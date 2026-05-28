#include "Logger.h"
#include "Windows.h"

namespace Engine::Base::Logger {

	void Log(const std::string& message)
	{
		OutputDebugStringA(message.c_str());

	}

}
