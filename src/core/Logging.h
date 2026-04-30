#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <cstdint>

#include <fmt/format.h>
#include <fmt/base.h>

inline bool ENABLE_DEBUG_LOGS =
#ifdef NDEBUG
false;
#else
true;
#endif

class Logging
{
public:
	struct LogMessage
	{
		uint32_t threadID;
		std::string text;
	};

	// -----------------------------
	// Buffered logging (thread-safe)
	// -----------------------------
	template<typename... Args>
	void Log(uint32_t threadID, fmt::format_string<Args...> format, Args&&... args)
	{
		if (!ENABLE_DEBUG_LOGS) return;

		std::string formattedText = fmt::format(
			format,
			std::forward<Args>(args)...
		);

		std::scoped_lock lock(logMutex);

		logMessages.push_back(
			{ threadID, std::move(formattedText) }
		);
	}

	void Log(uint32_t threadID, const std::string& text)
	{
		if (!ENABLE_DEBUG_LOGS) return;

		std::scoped_lock lock(logMutex);

		logMessages.push_back(
			{ threadID, text }
		);
	}

	// -----------------------------
	// Immediate print (thread-safe)
	// -----------------------------
	template<typename... Args>
	void Print(fmt::format_string<Args...> format, Args&&... args)
	{
		std::scoped_lock lock(printMutex);

		fmt::println(
			format,
			std::forward<Args>(args)...
		);
	}

	// thread-tagged print
	template<typename... Args>
	void PrintThread(uint32_t threadID, fmt::format_string<Args...> format, Args&&... args)
	{
		if (!ENABLE_DEBUG_LOGS) return;

		std::scoped_lock lock(printMutex);

		fmt::print("[Thread {}] ", threadID);
		fmt::println(
			format,
			std::forward<Args>(args)...
		);
	}

	// -----------------------------
	// Flush buffered logs
	// -----------------------------
	void FlushLogs()
	{
		std::vector<LogMessage> localMessages;

		{
			std::scoped_lock lock(logMutex);
			localMessages.swap(logMessages);
		}

		for (const LogMessage& msg : localMessages)
		{
			fmt::println(
				"[Thread {}] {}",
				msg.threadID,
				msg.text
			);
		}
	}

private:
	std::vector<LogMessage> logMessages;
	std::mutex logMutex;

	std::mutex printMutex; // separate to avoid contention with logging
};
