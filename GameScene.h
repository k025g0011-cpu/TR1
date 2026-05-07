#pragma once
#include "CellAutomaton.h"
#include "KamataEngine.h"

class GameScene {
private:
	uint32_t cellTextureHandle_ = 0;
	KamataEngine::Model* cellModel_ = nullptr;
	KamataEngine::Camera camera_;

	CellAutomaton* cellAutomaton_ = nullptr;

	// 入力
	KamataEngine::Input* input_ = nullptr;

	// カーソル用のモデル（ハイライト表示）
	KamataEngine::Model* cursorModel_ = nullptr;
	KamataEngine::WorldTransform cursorTransform_;

	float deltaTime_ = 1.0f / 60.0f;

public:
	GameScene() = default;
	~GameScene();

	void Initialize();
	void Update();
	void Draw();
};