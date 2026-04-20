#pragma once
#include <Windows.h>
#include <cstdint>

/// @brief Win32 ウィンドウの生成とメッセージ処理を管理するクラス
/// @details アプリケーションウィンドウの生成、破棄、メッセージポンプ処理を担当し、
///          描画基盤へ HWND と HINSTANCE を提供する。
namespace Engine::Base {

class WinApp
{

public:

	//クライアント領域のサイズ
	static const int32_t kClientWidth = 1280;
	static const int32_t kClientHeight = 720;

public:

	/// @brief ウィンドウメッセージを処理するコールバック関数
	/// @param hwnd 対象ウィンドウハンドル
	/// @param msg 受信メッセージ
	/// @param wparam メッセージの追加情報
	/// @param lparam メッセージの追加情報
	/// @return 処理結果
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	/// @brief アプリケーションウィンドウを生成する
	/// @param なし
	/// @return なし
	void Initialize();
	
	/// @brief アプリケーションウィンドウを破棄する
	/// @param なし
	/// @return なし
	void Finalize();

	//Getter
	HWND GetHwnd()const { return hwnd; }
	HINSTANCE GetHInstance()const { return wc.hInstance; }

	/// @brief メッセージキューを処理して終了要求を判定する
	/// @param なし
	/// @return 終了要求があれば true
	bool ProcessMessage();

private:
	void RegisterWindowClass();
	void CreateMainWindow();

	//ウィンドウ生成
	HWND hwnd = nullptr;
	WNDCLASS wc{};


};

}

