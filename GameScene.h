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

	// 財政：建設費のみ（収入・維持費なし）
	float cityBalance_ = 5000.0f;

	bool TryBuildCell(int x, int z, CellType type);

public:
	GameScene() = default;
	~GameScene();

	void Initialize();
	void Update();
	void Draw();
};