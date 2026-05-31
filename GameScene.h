#pragma once
#include "CellAutomaton.h"
#include "KamataEngine.h"

class GameScene {
private:
	KamataEngine::Model* cellModel_ = nullptr;
	KamataEngine::Camera camera_;
	CellAutomaton* cellAutomaton_ = nullptr;
	KamataEngine::Input* input_ = nullptr;
	KamataEngine::DebugCamera* debugCamera_ = nullptr;

	float deltaTime_ = 1.0f / 60.0f;

	// ★ 財政管理
	float cityBalance_ = 200000.0f;  // 市の残高（初期資金200000）
	float financeTimer_ = 0.0f;    // 収支更新タイマー
	float financeInterval_ = 1.0f; // 収支更新間隔（秒）
	bool isBankrupt_ = false;      // 財政破綻フラグ

	// 建物配置時に建設費を引く
	bool TryBuildCell(int x, int z, CellType type); // 資金があれば建設

public:
	GameScene() = default;
	~GameScene();

	void Initialize();
	void Update();
	void Draw();
};