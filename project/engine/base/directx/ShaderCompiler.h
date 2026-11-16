#pragma once

#include <Windows.h>
#include <dxcapi.h>
#include <wrl.h>
#include <string>
#include <map>
#include <cstdint>

namespace Engine::Base {

class ShaderCompiler {
public:
	// Use on the rendering/setup thread. Compile rechecks preprocessed input;
	// this cache does not replace live PSOs or watch files in the background.
	void Initialize();
	Microsoft::WRL::ComPtr<IDxcBlob> Compile(
		const std::wstring& filePath,
		const wchar_t* profile);
	void ClearCache();
	size_t GetCacheSize() const { return cache_.size(); }
	uint64_t GetCacheHitCount() const { return cacheHitCount_; }
	uint64_t GetCompileCount() const { return compileCount_; }

private:
	struct CacheEntry {
		std::string preprocessedSource;
		Microsoft::WRL::ComPtr<IDxcBlob> blob;
	};
	std::map<std::pair<std::wstring, std::wstring>, CacheEntry> cache_;
	uint64_t cacheHitCount_ = 0;
	uint64_t compileCount_ = 0;
	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_;
};

}
