#include "GameScene.h"
#include "WorldTransform.h"
#include <2d/ImGuiManager.h>

using namespace KamataEngine;

static CellType currentPlacingType = CellType::ROAD;

GameScene::~GameScene() {
	delete cellModel_;
	delete cursorModel_;
	delete cellAutomaton_;
	delete debugCamera_;
}

void GameScene::Initialize() {
	cellTextureHandle_ = TextureManager::Load("white1x1.png");
	cellModel_ = Model::Create();
	cursorModel_ = Model::Create();

	input_ = Input::GetInstance();

	camera_.Initialize();
	camera_.translation_ = {15.0f, 35.0f, -20.0f};
	camera_.rotation_.x = 0.8f;
	camera_.rotation_.y = 0.0f;
	camera_.UpdateMatrix();

	PrimitiveDrawer* primitiveDrawer = PrimitiveDrawer::GetInstance();
	primitiveDrawer->SetCamera(&camera_);

	cursorTransform_.Initialize();

	cellAutomaton_ = new CellAutomaton();
	cellAutomaton_->Initialize(cellModel_, &camera_);

	// ★ 初期都市配置（グリッド中央付近に小さな街を作る）
	auto P = [&](int x, int z, CellType t) { cellAutomaton_->PlaceCell(x, z, t); };
	using C = CellType;

	// 横道路（z=13, x=10〜19）
	for (int x = 10; x <= 19; ++x)
		P(x, 13, C::ROAD);
	// 縦道路（x=14, z=10〜19）
	for (int z = 10; z <= 19; ++z)
		P(14, z, C::ROAD);

	// 住宅ブロック（道路の左上）
	P(11, 11, C::RESIDENTIAL);
	P(12, 11, C::RESIDENTIAL);
	P(11, 12, C::RESIDENTIAL);
	P(12, 12, C::RESIDENTIAL);

	// 商業ブロック（道路の右上）
	P(15, 11, C::COMMERCIAL);
	P(16, 11, C::COMMERCIAL);
	P(15, 12, C::COMMERCIAL);

	// 工業ブロック（道路の左下）
	P(11, 14, C::INDUSTRIAL);
	P(12, 14, C::INDUSTRIAL);
	P(11, 15, C::INDUSTRIAL);

	// 公園（道路の右下）
	P(15, 14, C::PARK);
	P(16, 14, C::PARK);
	P(15, 15, C::PARK);
	P(16, 15, C::PARK);

	// 追加の住宅（道路沿い）
	P(11, 10, C::RESIDENTIAL);
	P(13, 10, C::RESIDENTIAL);
	P(17, 11, C::RESIDENTIAL);
	P(18, 12, C::RESIDENTIAL);
	P(17, 15, C::RESIDENTIAL);
	P(18, 15, C::RESIDENTIAL);

	// ★ 初期配置をターン0としてタイムラインに記録
	cellAutomaton_->SaveInitialSnapshot();

	debugCamera_ = new DebugCamera(1280, 720);
	debugCamera_->Update();
}

void GameScene::Update() {
	// カーソル移動
	if (input_->TriggerKey(DIK_UP))
		cellAutomaton_->MoveCursor(0, 1);
	if (input_->TriggerKey(DIK_DOWN))
		cellAutomaton_->MoveCursor(0, -1);
	if (input_->TriggerKey(DIK_LEFT))
		cellAutomaton_->MoveCursor(-1, 0);
	if (input_->TriggerKey(DIK_RIGHT))
		cellAutomaton_->MoveCursor(1, 0);

	// 配置タイプ切り替え＋設置
	if (input_->TriggerKey(DIK_1)) {
		currentPlacingType = CellType::ROAD;
		cellAutomaton_->PlaceCellAtCursor(currentPlacingType);
	}
	if (input_->TriggerKey(DIK_2)) {
		currentPlacingType = CellType::RESIDENTIAL;
		cellAutomaton_->PlaceCellAtCursor(currentPlacingType);
	}
	if (input_->TriggerKey(DIK_3)) {
		currentPlacingType = CellType::COMMERCIAL;
		cellAutomaton_->PlaceCellAtCursor(currentPlacingType);
	}
	if (input_->TriggerKey(DIK_4)) {
		currentPlacingType = CellType::INDUSTRIAL;
		cellAutomaton_->PlaceCellAtCursor(currentPlacingType);
	}
	if (input_->TriggerKey(DIK_5)) {
		currentPlacingType = CellType::PARK;
		cellAutomaton_->PlaceCellAtCursor(currentPlacingType);
	}
	if (input_->TriggerKey(DIK_0)) {
		int x, z;
		cellAutomaton_->GetCursorPosition(x, z);
		cellAutomaton_->RemoveCell(x, z);
	}

	// デバッグカメラ更新
	if (!ImGui::GetIO().WantCaptureMouse) {
		debugCamera_->Update();
	}
	camera_.matView = debugCamera_->GetCamera().matView;
	camera_.matProjection = debugCamera_->GetCamera().matProjection;
	camera_.TransferMatrix();

	cellAutomaton_->Update(deltaTime_);

	// ImGui
	ImGuiManager* imguiManager = ImGuiManager::GetInstance();
	imguiManager->Begin();

	int cursorX, cursorZ;
	cellAutomaton_->GetCursorPosition(cursorX, cursorZ);

	// 都市統計
	ImGui::Begin("City Statistics");
	int totalPopulation = 0;
	float totalIncome = 0.0f;
	int cellCounts[6] = {0};
	float totalTraffic = 0.0f;
	int roadCount = 0;
	int gridSize = cellAutomaton_->GetGridSize();

	for (int x = 0; x < gridSize; x++) {
		for (int z = 0; z < gridSize; z++) {
			Cell* cell = cellAutomaton_->GetCell(x, z);
			if (cell && cell->type != CellType::EMPTY) {
				totalPopulation += cell->population;
				totalIncome += cell->income;
				cellCounts[static_cast<int>(cell->type)]++;
				if (cell->type == CellType::ROAD) {
					totalTraffic += cell->traffic;
					roadCount++;
				}
			}
		}
	}

	ImGui::Text("=== Economy ===");
	ImGui::Text("Population: %d", totalPopulation);
	ImGui::Text("Income: $%.1f/sec", totalIncome);

	ImGui::Separator();
	ImGui::Text("=== Buildings ===");
	ImGui::Text("Residential: %d", cellCounts[static_cast<int>(CellType::RESIDENTIAL)]);
	ImGui::Text("Commercial:  %d", cellCounts[static_cast<int>(CellType::COMMERCIAL)]);
	ImGui::Text("Industrial:  %d", cellCounts[static_cast<int>(CellType::INDUSTRIAL)]);
	ImGui::Text("Parks:       %d", cellCounts[static_cast<int>(CellType::PARK)]);
	ImGui::Text("Roads:       %d", cellCounts[static_cast<int>(CellType::ROAD)]);

	ImGui::Separator();
	ImGui::Text("=== Traffic ===");
	if (roadCount > 0) {
		float avg = totalTraffic / roadCount;
		ImGui::Text("Avg Traffic: %.1f%%", avg * 100.0f);
		if (avg < 0.3f)
			ImGui::TextColored(ImVec4(0, 1, 0, 1), "Status: Smooth");
		else if (avg < 0.7f)
			ImGui::TextColored(ImVec4(1, 1, 0, 1), "Status: Moderate");
		else
			ImGui::TextColored(ImVec4(1, 0, 0, 1), "Status: CONGESTED!");
	} else {
		ImGui::Text("No roads built");
	}
	ImGui::End(); // City Statistics ここで閉じる

	// ★ タイムラインを別ウィンドウに分離
	ImGui::Begin("Timeline");
	int historySize = cellAutomaton_->GetHistorySize();
	if (historySize > 0) {
		int currentIdx = cellAutomaton_->GetCurrentSnapshotIndex();
		if (ImGui::SliderInt("Turn", &currentIdx, 0, historySize - 1)) {
			cellAutomaton_->enableAutomaton_ = false;
			cellAutomaton_->isPlaying_ = false;
			cellAutomaton_->SeekToSnapshot(currentIdx);
		}
		int displayIdx = cellAutomaton_->GetCurrentSnapshotIndex();
		ImGui::Text("Turn %d / %d", displayIdx + 1, historySize);
		ImGui::Separator();
		if (cellAutomaton_->isPlaying_) {
			if (ImGui::Button("  Stop  ")) {
				cellAutomaton_->isPlaying_ = false;
			}
		} else {
			if (ImGui::Button("  Play  ")) {
				cellAutomaton_->isPlaying_ = true;
				cellAutomaton_->playTimer_ = 0.0f;
				// ★ 常にhistoryの先頭（index=0）から再生
				cellAutomaton_->SeekToSnapshot(0);
			}
		}
		ImGui::SameLine();
		ImGui::SliderFloat("Speed", &cellAutomaton_->playInterval_, 0.1f, 2.0f);
	} else {
		ImGui::TextDisabled("No history. Enable Automaton to record.");
	}
	ImGui::End(); // Timeline

	// コントロール
	ImGui::Begin("Controls");
	const char* typeNames[] = {"Empty", "Road", "Residential", "Commercial", "Industrial", "Park"};
	const ImVec4 typeColors[] = {
	    {0.5f, 0.5f, 0.5f, 1},
        {0.6f, 0.6f, 0.6f, 1},
        {0.2f, 0.8f, 0.2f, 1},
        {0.2f, 0.2f, 0.8f, 1},
        {0.8f, 0.6f, 0.1f, 1},
        {0.1f, 0.7f, 0.1f, 1}
    };

	ImGui::Text("=== Place Type ===");
	ImGui::TextColored(typeColors[static_cast<int>(currentPlacingType)], "Selected: %s", typeNames[static_cast<int>(currentPlacingType)]);
	ImGui::Separator();
	ImGui::Text("1:Road  2:Residential");
	ImGui::Text("3:Commercial  4:Industrial");
	ImGui::Text("5:Park  0:Remove");
	ImGui::Text("Arrow: Move Cursor");

	ImGui::Separator();
	ImGui::Text("Cursor: (%d, %d)", cursorX, cursorZ);

	Cell* cell = cellAutomaton_->GetCell(cursorX, cursorZ);
	if (cell && cell->type != CellType::EMPTY) {
		ImGui::TextColored(typeColors[static_cast<int>(cell->type)], "Cell: %s Lv.%d", typeNames[static_cast<int>(cell->type)], cell->level);
		ImGui::Text("Population: %d", cell->population);
		ImGui::Text("Income: $%.1f", cell->income);
		if (cell->type == CellType::ROAD)
			ImGui::Text("Traffic: %.1f%%", cell->traffic * 100.0f);

		// ★ 建物があるときだけ影響範囲の説明を出す
		ImGui::Separator();
		ImGui::TextColored(ImVec4(1, 1, 0, 1), "Yellow lines = influence range");
	}

	ImGui::Separator();
	ImGui::Checkbox("Enable Automaton", &cellAutomaton_->enableAutomaton_);
	ImGui::SliderFloat("Interval(s)", &cellAutomaton_->updateInterval_, 0.2f, 5.0f);

	ImGui::Separator();
	ImGui::Text("=== Camera ===");
	ImGui::TextColored(ImVec4(1, 1, 0, 1), "Left drag : Rotate");
	ImGui::TextColored(ImVec4(1, 1, 0, 1), "Wheel     : Zoom");
	ImGui::TextColored(ImVec4(1, 1, 0, 1), "Mid drag  : Pan");

	ImGui::End();

	imguiManager->End();
}

void GameScene::Draw() {
	Model::PreDraw();

	PrimitiveDrawer* primitiveDrawer = PrimitiveDrawer::GetInstance();

	// ★ PrimitiveDrawerを渡してDraw（内部でラインも描画）
	cellAutomaton_->Draw(primitiveDrawer);

	float gridSize = 30.0f;
	float interval = 1.0f;

	primitiveDrawer->DrawLine3d({0, 0.05f, 0}, {gridSize, 0.05f, 0}, {1, 1, 1, 1});
	primitiveDrawer->DrawLine3d({gridSize, 0.05f, 0}, {gridSize, 0.05f, gridSize}, {1, 1, 1, 1});
	primitiveDrawer->DrawLine3d({gridSize, 0.05f, gridSize}, {0, 0.05f, gridSize}, {1, 1, 1, 1});
	primitiveDrawer->DrawLine3d({0, 0.05f, gridSize}, {0, 0.05f, 0}, {1, 1, 1, 1});

	for (int i = 1; i < 30; ++i) {
		float pos = i * interval;
		primitiveDrawer->DrawLine3d({0, 0.05f, pos}, {gridSize, 0.05f, pos}, {0.6f, 0.6f, 0.6f, 0.8f});
		primitiveDrawer->DrawLine3d({pos, 0.05f, 0}, {pos, 0.05f, gridSize}, {0.6f, 0.6f, 0.6f, 0.8f});
	}

	Model::PostDraw();

	ImGuiManager* imguiManager = ImGuiManager::GetInstance();
	imguiManager->Draw();
}