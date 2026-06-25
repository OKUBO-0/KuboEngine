#include "ShaderCompiler.h"
#include "HResult.h"
#include "Logger.h"
#include "StringUtility.h"
#include <array>
#include <format>
#include <stdexcept>

namespace Engine::Base {

void ShaderCompiler::Initialize()
{
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
	Logger::Log(StringUtility::ConvertString(
		std::format(L"Begin CompileShader,path:{},profile:{}\n", filePath, profile)));

	Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource;
	ThrowIfFailed(dxcUtils_->LoadFile(filePath.c_str(), nullptr, &shaderSource),
		"IDxcUtils::LoadFile");
	const DxcBuffer shaderSourceBuffer{
		.Ptr = shaderSource->GetBufferPointer(),
		.Size = shaderSource->GetBufferSize(),
		.Encoding = DXC_CP_UTF8,
	};
	std::array<LPCWSTR, 9> arguments{
		filePath.c_str(), L"-E", L"main", L"-T", profile,
		L"-Zi", L"-Qembed_debug", L"-Od", L"-Zpr",
	};

	Microsoft::WRL::ComPtr<IDxcResult> shaderResult;
	ThrowIfFailed(dxcCompiler_->Compile(
		&shaderSourceBuffer, arguments.data(),
		static_cast<UINT32>(arguments.size()), includeHandler_.Get(),
		IID_PPV_ARGS(&shaderResult)),
		"IDxcCompiler3::Compile");

	Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError;
	ThrowIfFailed(shaderResult->GetOutput(
		DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr),
		"IDxcResult::GetOutput errors");
	HRESULT compileStatus = S_OK;
	ThrowIfFailed(shaderResult->GetStatus(&compileStatus),
		"IDxcResult::GetStatus");
	if (FAILED(compileStatus)) {
		const std::string detail = shaderError && shaderError->GetStringLength() != 0
			? shaderError->GetStringPointer()
			: "Shader compilation failed without diagnostic text";
		Logger::Log(detail);
		throw std::runtime_error(detail);
	}
	if (shaderError && shaderError->GetStringLength() != 0) {
		Logger::Log(shaderError->GetStringPointer());
	}

	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob;
	ThrowIfFailed(shaderResult->GetOutput(
		DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr),
		"IDxcResult::GetOutput object");
	Logger::Log(StringUtility::ConvertString(
		std::format(L"Compile succeeded,path:{},profile:{}\n", filePath, profile)));
	return shaderBlob;
}

}
