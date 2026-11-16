#include "ShaderCompiler.h"
#include "../EngineResourcePath.h"
#include "HResult.h"
#include "Logger.h"
#include "StringUtility.h"
#include <array>
#include <format>
#include <stdexcept>
#include <filesystem>
#include <vector>

namespace Engine::Base {

namespace {
void CheckShaderResult(IDxcResult* result, const std::filesystem::path& sourceFile)
{
	Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;
	ThrowIfFailed(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr),
		"IDxcResult::GetOutput errors");
	HRESULT status = S_OK;
	ThrowIfFailed(result->GetStatus(&status), "IDxcResult::GetStatus");
	if (FAILED(status)) {
		const std::string detail = errors && errors->GetStringLength() != 0
			? errors->GetStringPointer() : "Shader compilation failed without diagnostic text";
		Logger::Log(detail, Logger::Level::Error, sourceFile);
		throw std::runtime_error(detail);
	}
	if (errors && errors->GetStringLength() != 0) {
		Logger::Log(errors->GetStringPointer(), Logger::Level::Warning, sourceFile);
	}
}
}

void ShaderCompiler::ClearCache()
{
	cache_.clear();
	cacheHitCount_ = 0;
	compileCount_ = 0;
}

void ShaderCompiler::Initialize()
{
	ClearCache();
	ThrowIfFailed(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils_)),
		"DxcCreateInstance utils");
	ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler_)),
		"DxcCreateInstance compiler");
	ThrowIfFailed(dxcUtils_->CreateDefaultIncludeHandler(&includeHandler_),
		"IDxcUtils::CreateDefaultIncludeHandler");
}

Microsoft::WRL::ComPtr<IDxcBlob> ShaderCompiler::Compile(
	const std::wstring& filePath,
	const wchar_t* profile)
{
	if (!dxcUtils_ || !dxcCompiler_ || !includeHandler_) {
		throw std::logic_error("ShaderCompiler must be initialized before Compile");
	}
	if (filePath.empty() || !profile || !*profile) {
		throw std::invalid_argument("Shader path and profile must not be empty");
	}
	const std::wstring resolvedPath = std::filesystem::weakly_canonical(ResolveEngineResource(filePath)).wstring();
	const auto key = std::make_pair(resolvedPath, std::wstring(profile));

	Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource;
	ThrowIfFailed(dxcUtils_->LoadFile(resolvedPath.c_str(), nullptr, &shaderSource),
		"IDxcUtils::LoadFile");
	const DxcBuffer shaderSourceBuffer{
		.Ptr = shaderSource->GetBufferPointer(),
		.Size = shaderSource->GetBufferSize(),
		.Encoding = DXC_CP_UTF8,
	};
	std::array<LPCWSTR, 9> arguments{
		resolvedPath.c_str(), L"-E", L"main", L"-T", profile,
		L"-Zi", L"-Qembed_debug", L"-Od", L"-Zpr",
	};
	// DXC resolves includes and macros; compare actual input rather than timestamps.
	std::vector<LPCWSTR> preprocessArguments(arguments.begin(), arguments.end());
	preprocessArguments.push_back(L"-P");
	Microsoft::WRL::ComPtr<IDxcResult> preprocessedResult;
	ThrowIfFailed(dxcCompiler_->Compile(&shaderSourceBuffer, preprocessArguments.data(),
		static_cast<UINT32>(preprocessArguments.size()), includeHandler_.Get(), IID_PPV_ARGS(&preprocessedResult)),
		"IDxcCompiler3::Compile preprocessing");
	CheckShaderResult(preprocessedResult.Get(), resolvedPath);
	Microsoft::WRL::ComPtr<IDxcBlobUtf8> preprocessed;
	ThrowIfFailed(preprocessedResult->GetOutput(DXC_OUT_HLSL, IID_PPV_ARGS(&preprocessed), nullptr),
		"IDxcResult::GetOutput preprocessed HLSL");
	if (!preprocessed) { throw std::runtime_error("DXC returned no preprocessed HLSL"); }
	const std::string source(preprocessed->GetStringPointer(), preprocessed->GetStringLength());
	if (const auto found = cache_.find(key); found != cache_.end() && found->second.preprocessedSource == source) {
		++cacheHitCount_;
		return found->second.blob;
	}
	Logger::Log(StringUtility::ConvertString(
		std::format(L"Begin CompileShader,path:{},profile:{}\n", resolvedPath, profile)));
	const DxcBuffer expandedBuffer{source.data(), source.size(), DXC_CP_UTF8};

	Microsoft::WRL::ComPtr<IDxcResult> shaderResult;
	++compileCount_;
	ThrowIfFailed(dxcCompiler_->Compile(
		&expandedBuffer, arguments.data(),
		static_cast<UINT32>(arguments.size()), nullptr,
		IID_PPV_ARGS(&shaderResult)),
		"IDxcCompiler3::Compile");

	CheckShaderResult(shaderResult.Get(), resolvedPath);

	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob;
	ThrowIfFailed(shaderResult->GetOutput(
		DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr),
		"IDxcResult::GetOutput object");
	// Failed compilation never replaces the last successful entry.
	cache_.insert_or_assign(key, CacheEntry{source, shaderBlob});
	Logger::Log(StringUtility::ConvertString(
		std::format(L"Compile succeeded,path:{},profile:{}\n", filePath, profile)));
	return shaderBlob;
}

}
