#pragma once
#include "CellAutomaton.h"
#include "GameParameters.h"
#include "KamataEngine.h"

/// <summary>
/// ゲームのメインシーンを管理するクラス。
/// プレイヤーの入力受付、資金管理、ImGuiによる各種ログ・ダッシュボードの描画、
/// およびゲームループ（Initialize, Update, Draw）の統括を行う。
/// </summary>
class GameScene {
private:
	// ── 1. エンジン・システム関連 ──
	KamataEngine::Model* cellModel_ = nullptr;         // 描画用3Dモデル
	KamataEngine::Camera camera_;                      // メインカメラ
	KamataEngine::Input* input_ = nullptr;             // 入力デバイス
	KamataEngine::DebugCamera* debugCamera_ = nullptr; // デバッグ用カメラ

	// ── 2. ゲームロジック関連 ──
	CellAutomaton* cellAutomaton_ = nullptr; // 都市シミュレータ本体
	GameParameters params_;                  // ゲームバランス調整用パラメータ

	float deltaTime_ = 1.0f / 60.0f; // 1フレームの経過時間
	float cityBalance_ = 5000.0f;    // プレイヤーの所持金

	// ── 3. 内部サブルーチン ──
	void LoadParameters();                          // JSONからの読み込み
	void SaveParameters();                          // JSONへの保存
	void UpdateUI();                                // ImGui描画の独立処理
	bool TryBuildCell(int x, int z, CellType type); // 建設処理（資金チェック付き）

public:
	GameScene() = default;
	~GameScene();

	void Initialize();
	void Update();
	void Draw();
};