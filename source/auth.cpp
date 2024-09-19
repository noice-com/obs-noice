// Copyright (C) 2024 Noice Inc.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "auth.hpp"
#include "common.hpp"
#include <util/util-curl.hpp>
#include <nlohmann/json.hpp>
#include <ctime>
#include <sstream>
#include <chrono>
#include <regex>

std::time_t timegm_hack(std::tm *t)
{
#ifdef _WIN32
	// Use _mkgmtime on Windows to convert std::tm to time_t (UTC)
	return _mkgmtime(t);
#else
	// Use timegm on POSIX-compliant systems (Linux, macOS)
	return ::timegm(t);
#endif
}

static std::optional<std::time_t> parse_iso3339(const std::string &iso3339)
{
	// Define regex pattern for ISO 3339
	std::regex iso3339_regex(R"((\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(\.(\d{0,12}))?(Z|([+-]\d{2}):(\d{2})))",
				 std::regex_constants::ECMAScript);

	std::smatch matches;
	if (!std::regex_match(iso3339, matches, iso3339_regex)) {
		return std::nullopt; // Return empty optional if regex does not match
	}

	// Extract matched components
	int year = std::stoi(matches[1].str());
	int month = std::stoi(matches[2].str());
	int day = std::stoi(matches[3].str());
	int hour = std::stoi(matches[4].str());
	int minute = std::stoi(matches[5].str());
	int second = std::stoi(matches[6].str());

	// Handle fractional seconds
	std::chrono::milliseconds milliseconds(0);
	if (matches[8].matched) {
		std::string fractional = matches[8].str(); // Use the whole fractional part
		if (fractional.size() > 3)
			fractional = fractional.substr(0, 3); // Keep up to milliseconds
		while (fractional.size() < 3)
			fractional += "0"; // Pad to 3 digits
		milliseconds = std::chrono::milliseconds(std::stoi(fractional));
	}

	// Handle timezone
	bool utc = matches[9].str() == "Z";
	std::chrono::minutes offset(0);
	if (!utc && matches[10].matched && matches[11].matched) {
		int tzHour = std::stoi(matches[10].str());
		int tzMinute = std::stoi(matches[11].str());
		offset = std::chrono::minutes(tzHour * 60 + (tzHour < 0 ? -tzMinute : tzMinute));
	}

	// Fill std::tm structure for local time
	std::tm t = {};
	t.tm_year = year - 1900;
	t.tm_mon = month - 1;
	t.tm_mday = day;
	t.tm_hour = hour;
	t.tm_min = minute;
	t.tm_sec = second;

	// Convert std::tm to time_t (local time)
	std::time_t time_tt = timegm_hack(&t);

	if (time_tt == -1) {
		return std::nullopt; // Invalid time
	}

	if (!utc) {
		time_tt -= std::chrono::duration_cast<std::chrono::seconds>(offset).count();
	}

	return time_tt;
}

noice::auth::~auth() {}

noice::auth::auth() : _token_expiration(0), _access_token(""), _refresh_token(""), _uid("") {}

std::shared_ptr<noice::auth> noice::auth::_instance = nullptr;

void noice::auth::initialize()
{
	if (!noice::auth::_instance)
		noice::auth::_instance = std::make_shared<noice::auth>();
}

void noice::auth::finalize()
{
	noice::auth::_instance.reset();
}

std::shared_ptr<noice::auth> noice::auth::instance()
{
	return noice::auth::_instance;
}

bool noice::auth::handle_signin_response(const std::string &res)
{
	nlohmann::json json_res;

	try {
		json_res = nlohmann::json::parse(res);
	} catch (...) {
		return false;
	}

	if (!json_res.contains("auth")) {
		return false;
	}

	auto auth = json_res["auth"];

	if (!auth.contains("expiresAt") || !auth.contains("token") || !auth.contains("refreshToken") || !auth.contains("uid")) {
		return false;
	}

	std::string expires_at = auth["expiresAt"].template get<std::string>();
	std::string token = auth["token"].template get<std::string>();
	std::string refresh_token = auth["refreshToken"].template get<std::string>();
	std::string uid = auth["uid"].template get<std::string>();

	auto ea = parse_iso3339(expires_at);
	if (!ea) {
		DLOG_ERROR("failed to parse timestamp");
		return false;
	}

	_token_expiration = static_cast<long long>(*ea);
	_access_token = std::move(token);
	_refresh_token = std::move(refresh_token);
	_uid = std::move(uid);

	return true;
}

bool noice::auth::is_token_valid()
{
	if (_access_token.empty()) {
		return false;
	}

	return true;
}

bool noice::auth::is_token_expired()
{
	auto now = std::chrono::system_clock::now().time_since_epoch();
	auto seconds_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(now).count();

	return seconds_since_epoch >= static_cast<decltype(seconds_since_epoch)>(_token_expiration) - 60;
}

bool noice::auth::sign_in()
{
	auto cfg = noice::configuration::instance();
	std::string stream_key = cfg->stream_key();

	nlohmann::json payload = {
		{"streamKey", stream_key},
		{"sessionTokenMode", "SESSION_TOKEN_MODE_RESPONSE"},
	};

	std::string json = payload.dump();

	std::string endpoint = noice::get_api_endpoint("v4/auth:signin");

	util::curl c;
	c.set_option(CURLOPT_URL, endpoint);
	c.set_option(CURLOPT_POST, true);
	c.set_header("Content-Type", "application/json");
	c.set_option(CURLOPT_POSTFIELDS, json.c_str());

	std::ostringstream stream;

	auto cb = [&stream](void *data, size_t size, size_t nmemb) -> size_t {
		const char *res = reinterpret_cast<char *>(data);
		stream.write(res, size * nmemb);

		return size * nmemb;
	};

	c.set_write_callback(cb);

	CURLcode code = c.perform();
	if (code != CURLE_OK) {
		DLOG_WARNING("Sign-in request failed.");
		return false;
	}

	long response_code = -1;
	c.get_info(CURLINFO_RESPONSE_CODE, response_code);

	if (response_code != 200) {
		DLOG_WARNING("sign in request failed with response code: %ld", response_code);
		return false;
	}

	return this->handle_signin_response(stream.str());
}

bool noice::auth::refresh_token()
{
	if (_refresh_token == "") {
		return false;
	}

	DLOG_INFO("refreshing access token");

	nlohmann::json payload = {
		{"refreshToken", _refresh_token},
		{"app", "noice_obs_plugin"},
		{"clientId", _uid},
	};

	std::string json = payload.dump();

	std::string endpoint = noice::get_api_endpoint("/v4/auth/session/session:refresh");

	util::curl c;
	c.set_option(CURLOPT_URL, endpoint);
	c.set_option(CURLOPT_POST, true);
	c.set_header("Content-Type", "application/json");
	c.set_option(CURLOPT_POSTFIELDS, json.c_str());
	c.set_option(CURLOPT_FOLLOWLOCATION, true);
	c.set_option(CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);

	std::ostringstream stream;

	auto cb = [&stream](void *data, size_t size, size_t nmemb) -> size_t {
		const char *res = reinterpret_cast<char *>(data);
		stream.write(res, size * nmemb);

		return size * nmemb;
	};

	c.set_write_callback(cb);

	CURLcode code = c.perform();
	if (code != CURLE_OK) {
		DLOG_WARNING("token refresh request failed.");
		return false;
	}

	long response_code = -1;
	c.get_info(CURLINFO_RESPONSE_CODE, response_code);

	if (response_code != 200) {
		DLOG_WARNING("token refresh request failed with response code: %ld response: %s", response_code, stream.str().c_str());
		reset_access_token();
		return false;
	}

	return handle_signin_response(stream.str());
}

std::optional<std::string> noice::auth::get_access_token()
{
	std::unique_lock<std::mutex> lock(_lock);

	if (!is_token_valid()) {
		if (!sign_in()) {
			reset_access_token();
			return std::nullopt;
		}
	} else if (is_token_expired()) {
		if (!refresh_token()) {
			reset_access_token();
			return std::nullopt;
		}
	}

	return _access_token;
}

void noice::auth::reset_access_token()
{
	_access_token = "";
	_refresh_token = "";
	_token_expiration = 0;
}
