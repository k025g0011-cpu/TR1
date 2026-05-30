#include <Windows.h>
#include"kamataEngine.h"
#include"GameScene.h"



// 課題用　<変更> コメントアウト

/*
KAMATAENGINE　を構成する各クラスは基本的にkamataEngine　namespaceに所属している
(例) kamataEngine::Spriteなど

using namespaceを宣言しておくことで毎回 kamataEngine::を書くのを回避できる
*/
using namespace KamataEngine;

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	
	//タイトルバー変更
	KamataEngine::Initialize(L"LE2B_10_グンジ_ソラ_TR");

	// DirectXCommonインスタンスの取得
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();

	//ゲームシーンのインスタンス生成
	GameScene* gameScene = new GameScene();

	//ゲームシーンの初期化
	gameScene->Initialize();


	while (true) {
		if (KamataEngine::Update()) {
			break;
		}

		//ゲームシーンの更新
		gameScene->Update();


		//描画開始
		dxCommon->PreDraw();

		//ゲームシーンの描画
		gameScene->Draw();

		//描画終了
		dxCommon->PostDraw();
	}



	delete gameScene;
	gameScene = nullptr;






	KamataEngine::Finalize();

	return 0;
}
