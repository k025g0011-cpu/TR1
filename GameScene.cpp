#include "GameScene.h"
#include "WorldTransform.h"
#include <2d/ImGuiManager.h>

using namespace KamataEngine;

static CellType currentPlacingType = CellType::ROAD;

GameScene::~GameScene() {
	delete cellModel_;
	delete cellAutomaton_;
	delete debugCamera_;
}

void GameScene::Initialize() {
	cellModel_ = Model::Create();
	input_ = Input::GetInstance();

	camera_.Initialize();
	camera_.translation_ = {15.0f, 35.0f, -20.0f};
	camera_.rotation_.x = 0.8f;
	camera_.UpdateMatrix();

	PrimitiveDrawer::GetInstance()->SetCamera(&camera_);

	cellAutomaton_ = new CellAutomaton();
	cellAutomaton_->Initialize(cellModel_, &camera_);

	debugCamera_ = new DebugCamera(1280, 720);
	debugCamera_->Update();
}

// ★ 建設費を払って建物を置く
// 資金不足なら建設できない（falseを返す）
bool GameScene::TryBuildCell(int x, int z, CellType type) {
	float cost = GetBuildingCost(type).buildCost;

	// 既存の建物を撤去する場合はコストなし
	Cell* existing = cellAutomaton_->GetCell(x, z);
	if (existing && existing->type != CellType::EMPTY) {
		// 撤去は無料（将来的に撤去費を追加してもよい）
	}

	if (cityBalance_ < cost)
		return false; // 資金不足

	cityBalance_ -= cost;
	cellAutomaton_->PlaceCell(x, z, type);
	return true;
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

	// 建物配置（資金チェックあり）
	int cx, cz;
	cellAutomaton_->GetCursorPosition(cx, cz);

	auto tryPlace = [&](CellType type) {
		currentPlacingType = type;
		TryBuildCell(cx, cz, type);
	};

	if (input_->TriggerKey(DIK_1))
		tryPlace(CellType::ROAD);
	if (input_->TriggerKey(DIK_2))
		tryPlace(CellType::RESIDENTIAL);
	if (input_->TriggerKey(DIK_3))
		tryPlace(CellType::COMMERCIAL);
	if (input_->TriggerKey(DIK_4))
		tryPlace(CellType::INDUSTRIAL);
	if (input_->TriggerKey(DIK_5))
		tryPlace(CellType::PARK);
	if (input_->TriggerKey(DIK_0))
		cellAutomaton_->RemoveCell(cx, cz); // 撤去は無料

	// デバッグカメラ
	if (!ImGui::GetIO().WantCaptureMouse)
		debugCamera_->Update();
	camera_.matView = debugCamera_->GetCamera().matView;
	camera_.matProjection = debugCamera_->GetCamera().matProjection;
	camera_.TransferMatrix();

	// シミュレーション更新
	cellAutomaton_->Update(deltaTime_);

	// ★ 財政更新（毎financeInterval_秒ごと）
	if (cellAutomaton_->enableSimulation_) {
		financeTimer_ += deltaTime_;
		if (financeTimer_ >= financeInterval_) {
			financeTimer_ = 0.0f;

			float income = cellAutomaton_->GetTotalIncome();
			float maintenance = cellAutomaton_->GetTotalMaintenance();
			float netProfit = income - maintenance;

			cityBalance_ += netProfit;

			// 財政破綻チェック
			isBankrupt_ = (cityBalance_ < 0.0f);
		}
	}

	// ImGui
	ImGuiManager::GetInstance()->Begin();

	// ── City Statistics ──
	ImGui::Begin("City Statistics");

	// 財政サマリー
	ImGui::Text("=== Finance ===");
	ImGui::TextColored(ImVec4(1, 1, 0, 1), "Balance:  $%.0f", cityBalance_);
	float income = cellAutomaton_->GetTotalIncome();
	float maintenance = cellAutomaton_->GetTotalMaintenance();
	float net = income - maintenance;
	ImGui::Text("Income:      +$%.1f/s", income);
	ImGui::Text("Maintenance: -$%.1f/s", maintenance);

	// 収支がプラスかマイナスかで色を変える
	if (net >= 0)
		ImGui::TextColored(ImVec4(0, 1, 0, 1), "Net:      +$%.1f/s", net);
	else
		ImGui::TextColored(ImVec4(1, 0, 0, 1), "Net:      -$%.1f/s", -net);

	if (isBankrupt_)
		ImGui::TextColored(ImVec4(1, 0, 0, 1), "!! BANKRUPT !!");

	ImGui::Separator();
	ImGui::Text("=== Population ===");
	ImGui::Text("Total: %d", cellAutomaton_->GetTotalPopulation());

	ImGui::End();

	// ── Controls ──
	ImGui::Begin("Controls");

	const char* typeNames[] = {"Empty", "Road", "Residential", "Commercial", "Industrial", "Park"};
	const ImVec4 typeColors[] = {
	    {0.5f, 0.5f, 0.5f, 1},
        {0.6f, 0.6f, 0.6f, 1},
        {0.2f, 0.9f, 0.2f, 1},
        {0.2f, 0.2f, 0.9f, 1},
        {0.9f, 0.6f, 0.1f, 1},
        {0.1f, 0.8f, 0.1f, 1}
    };

	// 選択中タイプと建設費表示
	ImGui::Text("=== Place Type ===");
	ImGui::TextColored(typeColors[static_cast<int>(currentPlacingType)], "Selected: %s", typeNames[static_cast<int>(currentPlacingType)]);

	// 建設費一覧
	ImGui::Separator();
	ImGui::Text("Build costs:");
	CellType types[] = {CellType::ROAD, CellType::RESIDENTIAL, CellType::COMMERCIAL, CellType::INDUSTRIAL, CellType::PARK};
	int keys[] = {1, 2, 3, 4, 5};
	for (int i = 0; i < 5; ++i) {
		auto cost = GetBuildingCost(types[i]);
		ImGui::TextColored(typeColors[static_cast<int>(types[i])], "%d:%-12s $%.0f (maint:$%.0f)", keys[i], typeNames[static_cast<int>(types[i])], cost.buildCost, cost.maintenanceCost);
	}
	ImGui::Text("0: Remove (free)");
	ImGui::Text("Arrow: Move Cursor");

	// カーソル位置と建物情報
	ImGui::Separator();
	ImGui::Text("Cursor: (%d, %d)", cx, cz);
	Cell* cell = cellAutomaton_->GetCell(cx, cz);
	if (cell && cell->type != CellType::EMPTY) {
		ImGui::TextColored(typeColors[static_cast<int>(cell->type)], "Cell: %s  Lv.%d", typeNames[static_cast<int>(cell->type)], cell->level);
		ImGui::Text("Population: %d", cell->population);
		ImGui::Text("Income:     $%.1f", cell->income);
		ImGui::Text("Maint:     -$%.1f", GetBuildingCost(cell->type).maintenanceCost);

		// 道路接続状態
		const int dx[] = {0, 0, 1, -1}, dz[] = {1, -1, 0, 0};
		bool hasRoad = false;
		for (int i = 0; i < 4; ++i) {
			Cell* n = cellAutomaton_->GetCell(cx + dx[i], cz + dz[i]);
			if (n && n->type == CellType::ROAD) {
				hasRoad = true;
				break;
			}
		}
		ImGui::TextColored(hasRoad ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1), hasRoad ? "Road: Connected" : "Road: NOT connected");
	}

	ImGui::Separator();
	ImGui::Checkbox("Enable Simulation", &cellAutomaton_->enableSimulation_);
	ImGui::SliderFloat("Interval(s)", &cellAutomaton_->simInterval_, 0.2f, 3.0f);

	ImGui::Separator();
	ImGui::Text("=== Camera ===");
	ImGui::TextColored(ImVec4(1, 1, 0, 1), "Left drag : Rotate");
	ImGui::TextColored(ImVec4(1, 1, 0, 1), "Wheel     : Zoom");
	ImGui::TextColored(ImVec4(1, 1, 0, 1), "Mid drag  : Pan");

	ImGui::End();

	ImGuiManager::GetInstance()->End();
}

void GameScene::Draw() {
	Model::PreDraw();

	PrimitiveDrawer* pd = PrimitiveDrawer::GetInstance();
	cellAutomaton_->Draw(pd);

	const float gs = 30.0f, y = 0.05f;
	pd->DrawLine3d({0, y, 0}, {gs, y, 0}, {1, 1, 1, 1});
	pd->DrawLine3d({gs, y, 0}, {gs, y, gs}, {1, 1, 1, 1});
	pd->DrawLine3d({gs, y, gs}, {0, y, gs}, {1, 1, 1, 1});
	pd->DrawLine3d({0, y, gs}, {0, y, 0}, {1, 1, 1, 1});
	for (int i = 1; i < 30; ++i) {
		float p = static_cast<float>(i);
		pd->DrawLine3d({0, y, p}, {gs, y, p}, {0.5f, 0.5f, 0.5f, 0.6f});
		pd->DrawLine3d({p, y, 0}, {p, y, gs}, {0.5f, 0.5f, 0.5f, 0.6f});
	}

	Model::PostDraw();
	ImGuiManager::GetInstance()->Draw();
}