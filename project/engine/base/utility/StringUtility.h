#pragma once
#include <string>

namespace Engine::Base::StringUtility {

	//DirectX12デバイス
	std::wstring ConvertString(const std::string& str);
	//DXGIファクトリ
	std::string ConvertString(const std::wstring& str);
}
