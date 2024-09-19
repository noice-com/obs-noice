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

#pragma once

#include <memory>
#include <mutex>
#include <optional>

namespace noice {
class auth {
private:
	std::mutex _lock;

	long long _token_expiration;
	std::string _access_token;
	std::string _refresh_token;
	std::string _uid;

public:
	virtual ~auth();
	auth();

private:
	bool sign_in();
	bool refresh_token();
	void reset_access_token();
	bool handle_signin_response(const std::string &res);
	bool is_token_valid();
	bool is_token_expired();

public:
	virtual std::optional<std::string> get_access_token();

private /* Singleton */:
	static std::shared_ptr<noice::auth> _instance;

public /* Singleton */:
	static void initialize();

	static void finalize();

	static std::shared_ptr<noice::auth> instance();
};
} // namespace noice
