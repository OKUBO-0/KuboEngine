#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <cstdint>
#include <iomanip>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <string>

namespace Engine::Base {

inline std::string FormatHResultMessage(HRESULT result)
{
	char* systemMessage = nullptr;
	const DWORD length = FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		static_cast<DWORD>(result),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<char*>(&systemMessage),
		0,
		nullptr);

	std::string message;
	if (length != 0 && systemMessage) {
		message.assign(systemMessage, length);
		while (!message.empty() &&
			(message.back() == '\r' || message.back() == '\n')) {
			message.pop_back();
		}
	}
	if (systemMessage) {
		LocalFree(systemMessage);
	}
	return message;
}

inline void ThrowIfFailed(
	HRESULT result,
	const char* operation,
	const std::source_location location =
		std::source_location::current())
{
	if (SUCCEEDED(result)) {
		return;
	}

	std::ostringstream message;
	message << operation
		<< " failed with HRESULT 0x"
		<< std::hex << std::uppercase
		<< static_cast<uint32_t>(result);
	const std::string systemMessage = FormatHResultMessage(result);
	if (!systemMessage.empty()) {
		message << " (" << systemMessage << ')';
	}
	message << " at " << location.file_name()
		<< ':' << std::dec << location.line();
	throw std::runtime_error(message.str());
}

template <class T>
T* MapResource(
	ID3D12Resource* resource,
	const char* operation,
	UINT subresource = 0,
	const D3D12_RANGE* readRange = nullptr)
{
	if (!resource) {
		throw std::invalid_argument(
			std::string(operation) + " received a null resource");
	}

	T* mappedData = nullptr;
	ThrowIfFailed(
		resource->Map(
			subresource,
			readRange,
			reinterpret_cast<void**>(&mappedData)),
		operation);
	if (!mappedData) {
		throw std::runtime_error(
			std::string(operation) + " returned a null mapped address");
	}
	return mappedData;
}

}
