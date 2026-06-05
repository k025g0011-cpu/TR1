#pragma once
#include "CellAutomaton.h"
#include "KamataEngine.h"

/// <summary>
/// ゲームのメインシーンを管理するクラス。
/// プレイヤーの入力受付、資金管理、ImGuiによる各種ログ・ダッシュボードの描画、
/// およびゲームループ（Initialize, Update, Draw）の統括を行う。
/// </summary>
class GameScene {
private:
	// ── エンジン・グラフィックス関連のメンバ変数 ──
	KamataEngine::Model* cellModel_ = nullptr;         // 建物や床を描画するための3Dモデルのポインタ
	KamataEngine::Camera camera_;                      // 3D空間を見下ろす基本カメラ
	CellAutomaton* cellAutomaton_ = nullptr;           // 都市のデータとシミュレーション論理を司るコアクラス
	KamataEngine::Input* input_ = nullptr;             // キーボード入力を取得するためのインスタンス
	KamataEngine::DebugCamera* debugCamera_ = nullptr; // マウス操作で自由に視点を動かせるデバッグ用のカメラ

	// ── ゲームスピード・時間管理 ──
	float deltaTime_ = 1.0f / 60.0f; // 1フレームあたりの経過時間（固定60FPSを想定した約0.0166秒）

	// ── 国家財政・プレイヤーの資金 ──
	float cityBalance_ = 5000.0f; // プレイヤーの現在の手元資金（初期値は$5000）

	/// <summary>
	/// 指定された座標に建物の建設を試みる内部関数。
	/// 資金チェックを行い、足りていれば資金を減算して建設を実行する。
	/// </summary>
	/// <param name="x">グリッドのX座標 (0〜29)</param>
	/// <param name="z">グリッドのZ座標 (0〜29)</param>
	/// <param name="type">建設したい建物の種類</param>
	/// <returns>建設に成功した場合はtrue、資金不足で失敗した場合はfalse</returns>
	bool TryBuildCell(int x, int z, CellType type);

public:
	// コンストラクタとデストラクタ
	GameScene() = default;
	~GameScene();

	// ゲームループの基本3関数
	void Initialize(); // ゲーム開始時の初期設定
	void Update();     // 毎フレームの更新処理（入力、シミュレーション進行、UI）
	void Draw();       // 毎フレームの描画処理（3Dモデル、グリッド、ImGui）
};