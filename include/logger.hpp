/**
 * @file logger.hpp
 * @brief Logger class for logging things into a file.
 * @date 2023-04-06
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>

std::string str_wrap(const char* format, ...);

class Logger {
	public:
		Logger(bool _release, bool _log_switch, bool _log_stdout, int _log_level);

		const bool release;
		const bool log_switch;
		const int log_level;

		void config(int turn);
		void log(int level, const char* format, ...);
		void err(const char* format, ...);
		void err(const std::string& str);
		void raw(const char* format, ...);
		void flush(); // 每回合结束要flush
	private:
		int turn;
		char buffer[256];
		std::FILE* file;
};

std::string str_wrap(const char* format, ...) {
	static char buffer[256];
	va_list args;
	va_start(args, format);
	vsprintf(buffer, format, args);
	va_end(args);
	return std::string(buffer);
}

Logger::Logger(bool _release, bool _log_switch, bool _log_stdout, int _log_level)
: release(_release), log_switch(_log_switch), log_level(_log_level)
{
	if(log_switch) {
		if(_log_stdout) file = stdout;
		else file = fopen("log.log", "a");
	}
}
void Logger::config(int _turn) {
	turn = _turn;
}
void Logger::log(int level, const char* format, ...) {
	if(log_switch && level >= log_level && !release) {
		va_list args;
		va_start(args, format);
		vsprintf(buffer, format, args);
		va_end(args);
		fprintf(file, "turn%03d: %s\n", turn, buffer);
	}
}
void Logger::err(const char* format, ...) {
	if(!release) return;
	va_list args;
	va_start(args, format);
	vsprintf(buffer, format, args);
	va_end(args);
	fprintf(stderr, "%03d %s\n", turn, buffer);
}
void Logger::err(const std::string& str) {
	if(!release) return;
	fprintf(stderr, "%03d %s\n", turn, str.c_str());
}
void Logger::raw(const char* format, ...) {
	if(!log_switch) return;
	va_list args;
	va_start(args, format);
	vsprintf(buffer, format, args);
	va_end(args);
	fprintf(file, "%s", buffer);
}
void Logger::flush() {
	if(log_switch) fflush(file);
}
