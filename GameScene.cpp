#include "GameScene.h"
#include "WorldTransform.h"
#include <2d/ImGuiManager.h>

using namespace KamataEngine;

GameScene::~GameScene() {
	delete cellModel_;
	delete cursorModel_;
	delete cellAutomaton_;
}

void GameScene::Initialize() {
	cellTextureHandle_ = TextureManager::Load("white1x1.png");
	cellModel_ = Model::Create();
	cursorModel_ = Model::Create();

	// 入力の取得
	input_ = Input::GetInstance();

	// カメラの初期化（斜め上から見下ろす視点）
	camera_.Initialize();
	camera_.translation_ = {10.0f, 25.0f, -15.0f};
	camera_.rotation_.x = 0.8f;
	camera_.rotation_.y = 0.0f;
	camera_.UpdateMatrix();

	PrimitiveDrawer* primitiveDrawer = PrimitiveDrawer::GetInstance();
	primitiveDrawer->SetCamera(&camera_);

	// カーソルのトランスフォーム初期化
	cursorTransform_.Initialize();
	cursorTransform_.scale_ = {0.5f, 0.02f, 0.5f};
	cursorTransform_.translation_.y = 0.01f;

	// セルオートマトンの初期化
	cellAutomaton_ = new CellAutomaton();
	cellAutomaton_->Initialize(cellModel_, &camera_);
}

void GameScene::Update() {
	// キー入力処理
	static bool keyPressed[6] = {false}; // 連打防止

	// 矢印キーでカーソル移動
	if (input_->TriggerKey(DIK_UP)) {
		cellAutomaton_->MoveCursor(0, 1);
	}
	if (input_->TriggerKey(DIK_DOWN)) {
		cellAutomaton_->MoveCursor(0, -1);
	}
	if (input_->TriggerKey(DIK_LEFT)) {
		cellAutomaton_->MoveCursor(-1, 0);
	}
	if (input_->TriggerKey(DIK_RIGHT)) {
		cellAutomaton_->MoveCursor(1, 0);
	}

	// 数字キーで建物配置（1-6キー）
	if (input_->TriggerKey(DIK_1)) {
		cellAutomaton_->PlaceCellAtCursor(CellType::ROAD);
	}
	if (input_->TriggerKey(DIK_2)) {
		cellAutomaton_->PlaceCellAtCursor(CellType::RESIDENTIAL);
	}
	if (input_->TriggerKey(DIK_3)) {
		cellAutomaton_->PlaceCellAtCursor(CellType::COMMERCIAL);
	}
	if (input_->TriggerKey(DIK_4)) {
		cellAutomaton_->PlaceCellAtCursor(CellType::INDUSTRIAL);
	}
	if (input_->TriggerKey(DIK_5)) {
		cellAutomaton_->PlaceCellAtCursor(CellType::PARK);
	}
	if (input_->TriggerKey(DIK_0)) {
		int x, z;
		cellAutomaton_->GetCursorPosition(x, z);
		cellAutomaton_->RemoveCell(x, z);
	}

	// カーソル位置の更新（マスの中心に配置）
	int cursorX, cursorZ;
	cellAutomaton_->GetCursorPosition(cursorX, cursorZ);
	cursorTransform_.translation_.x = static_cast<float>(cursorX) + 0.5f; // マスの中心
	cursorTransform_.translation_.z = static_cast<float>(cursorZ) + 0.5f; // マスの中心
	WorldTransformUpdate(cursorTransform_);

	// セルオートマトンの更新
	cellAutomaton_->Update(deltaTime_);

	// ImGui
	ImGuiManager* imguiManager = ImGuiManager::GetInstance();
	imguiManager->Begin();

	ImGui::Begin("Controls");
	ImGui::Text("Arrow Keys: Move Cursor");
	ImGui::Text("1: Road");
	ImGui::Text("2: Residential");
	ImGui::Text("3: Commercial");
	ImGui::Text("4: Industrial");
	ImGui::Text("5: Park");
	ImGui::Text("0: Remove");
	ImGui::Separator();
	ImGui::Text("Cursor: (%d, %d)", cursorX, cursorZ);

	Cell* cell = cellAutomaton_->GetCell(cursorX, cursorZ);
	if (cell && cell->type != CellType::EMPTY) {
		const char* types[] = {"Empty", "Road", "Residential", "Commercial", "Industrial", "Park"};
		ImGui::Text("Cell: %s", types[static_cast<int>(cell->type)]);
		ImGui::Text("Level: %d", cell->level);
	}

	ImGui::Separator();
	ImGui::Checkbox("Enable Automaton", &cellAutomaton_->enableAutomaton_);

	ImGui::Separator();
	ImGui::Text("Camera Adjust:");
	ImGui::DragFloat3("Position", &camera_.translation_.x, 0.5f);
	ImGui::DragFloat3("Rotation", &camera_.rotation_.x, 0.01f);

	ImGui::End();

	imguiManager->End();
	camera_.UpdateMatrix();
}

void GameScene::Draw() {
	Model::PreDraw();

	// セルオートマトンの描画
	cellAutomaton_->Draw();

	// カーソルの描画（黄色っぽい薄い平面）
	uint32_t yellowTexture = TextureManager::Load("white1x1.png");
	cursorModel_->Draw(cursorTransform_, camera_, yellowTexture);

	// グリッド描画（20×20）
	PrimitiveDrawer* primitiveDrawer = PrimitiveDrawer::GetInstance();
	float gridSize = 20.0f;
	float interval = 1.0f;

	// 外枠を白で太めに描画
	primitiveDrawer->DrawLine3d({0, 0, 0}, {gridSize, 0, 0}, {1.0f, 1.0f, 1.0f, 1.0f});
	primitiveDrawer->DrawLine3d({gridSize, 0, 0}, {gridSize, 0, gridSize}, {1.0f, 1.0f, 1.0f, 1.0f});
	primitiveDrawer->DrawLine3d({gridSize, 0, gridSize}, {0, 0, gridSize}, {1.0f, 1.0f, 1.0f, 1.0f});
	primitiveDrawer->DrawLine3d({0, 0, gridSize}, {0, 0, 0}, {1.0f, 1.0f, 1.0f, 1.0f});

	// 内部のグリッド線を明るいグレーで描画
	for (int i = 1; i < 20; ++i) {
		float pos = i * interval;
		// 横線（Z方向）
		primitiveDrawer->DrawLine3d({0, 0, pos}, {gridSize, 0, pos}, {0.6f, 0.6f, 0.6f, 0.8f});
		// 縦線（X方向）
		primitiveDrawer->DrawLine3d({pos, 0, 0}, {pos, 0, gridSize}, {0.6f, 0.6f, 0.6f, 0.8f});
	}


	Model::PostDraw();

	ImGuiManager* imguiManager = ImGuiManager::GetInstance();
	imguiManager->Draw();
}