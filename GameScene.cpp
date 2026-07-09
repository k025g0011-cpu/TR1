#define NOMINMAX
#include "GameScene.h"
#include "WorldTransform.h"
#include "json.hpp"
#include <2d/ImGuiManager.h>
#include <algorithm>
#include <fstream>
using json = nlohmann::json;

using namespace KamataEngine;

// 現在選択している建物の種類
static CellType currentPlacingType = CellType::ROAD;

// =====================================================================
// 初期化・解放・JSON管理
// =====================================================================
GameScene::~GameScene() {
	delete cellModel_;
	delete cellAutomaton_;
	delete debugCamera_;
}

void GameScene::LoadParameters() {
	std::ifstream file("parameters.json");
	if (file.is_open()) {
		json j;
		file >> j;
		params_.costRoad = j.value("costRoad", params_.costRoad);
		params_.costResidential = j.value("costResidential", params_.costResidential);
		params_.costCommercial = j.value("costCommercial", params_.costCommercial);
		params_.costIndustrial = j.value("costIndustrial", params_.costIndustrial);
		params_.costPark = j.value("costPark", params_.costPark);
		params_.maintCommercial = j.value("maintCommercial", params_.maintCommercial);
		params_.maintIndustrial = j.value("maintIndustrial", params_.maintIndustrial);
		params_.ageGrace = j.value("ageGrace", params_.ageGrace);
		params_.ageDecay = j.value("ageDecay", params_.ageDecay);
		params_.ageMinEff = j.value("ageMinEff", params_.ageMinEff);
		file.close();
	}
}

void GameScene::SaveParameters() {
	json j = {
	    {"costRoad",        params_.costRoad       },
	    {"costResidential", params_.costResidential},
	    {"costCommercial",  params_.costCommercial },
	    {"costIndustrial",  params_.costIndustrial },
	    {"costPark",        params_.costPark       },
	    {"maintCommercial", params_.maintCommercial},
	    {"maintIndustrial", params_.maintIndustrial},
	    {"ageGrace",        params_.ageGrace       },
	    {"ageDecay",        params_.ageDecay       },
	    {"ageMinEff",       params_.ageMinEff      }
    };
	std::ofstream file("parameters.json");
	file << j.dump(4);
	file.close();
}

void GameScene::Initialize() {
	LoadParameters();

	cellModel_ = Model::Create();
	input_ = Input::GetInstance();

	camera_.Initialize();
	camera_.translation_ = {15.0f, 20.0f, -5.0f};
	camera_.rotation_.x = 0.8f;
	camera_.UpdateMatrix();

	PrimitiveDrawer::GetInstance()->SetCamera(&camera_);

	cellAutomaton_ = new CellAutomaton();
	cellAutomaton_->Initialize(cellModel_, &camera_, &params_);

	debugCamera_ = new DebugCamera(1280, 720);
	debugCamera_->Update();
}

// =====================================================================
// メインゲームループ (Update / Draw)
// =====================================================================
bool GameScene::TryBuildCell(int x, int z, CellType type) {
	float cost = cellAutomaton_->GetBuildingCost(type).buildCost;
	if (cityBalance_ < cost)
		return false;

	cityBalance_ -= cost;
	cellAutomaton_->PlaceCell(x, z, type);
	return true;
}

void GameScene::Update() {
	// ── 1. キーボード操作 ──
	if (input_->TriggerKey(DIK_UP))
		cellAutomaton_->MoveCursor(0, 1);
	if (input_->TriggerKey(DIK_DOWN))
		cellAutomaton_->MoveCursor(0, -1);
	if (input_->TriggerKey(DIK_LEFT))
		cellAutomaton_->MoveCursor(-1, 0);
	if (input_->TriggerKey(DIK_RIGHT))
		cellAutomaton_->MoveCursor(1, 0);

	// ── 2. 建設と撤去 ──
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
		cellAutomaton_->RemoveCell(cx, cz);

	if (input_->TriggerKey(DIK_TAB))
		cellAutomaton_->ToggleDisplayMode();

	// ── 3. カメラとシミュレーションの更新 ──
	camera_.UpdateMatrix();
	camera_.TransferMatrix();

	cellAutomaton_->Update(deltaTime_);
	cityBalance_ += cellAutomaton_->CollectIncome(); // 収益の回収

	// ── 4. UIの構築 ──
	UpdateUI();
}

void GameScene::Draw() {
	Model::PreDraw();
	PrimitiveDrawer* pd = PrimitiveDrawer::GetInstance();

	// 都市本体の描画
	cellAutomaton_->Draw(pd);

	// グリッド線（床）の描画
	const float gs = 30.0f, y = 0.05f;
	Vector4 white = {1, 1, 1, 1};
	Vector4 gray = {0.5f, 0.5f, 0.5f, 0.6f};

	pd->DrawLine3d({0, y, 0}, {gs, y, 0}, white);
	pd->DrawLine3d({gs, y, 0}, {gs, y, gs}, white);
	pd->DrawLine3d({gs, y, gs}, {0, y, gs}, white);
	pd->DrawLine3d({0, y, gs}, {0, y, 0}, white);

	for (int i = 1; i < 30; ++i) {
		float p = static_cast<float>(i);
		pd->DrawLine3d({0, y, p}, {gs, y, p}, gray);
		pd->DrawLine3d({p, y, 0}, {p, y, gs}, gray);
	}

	Model::PostDraw();

	// UIの描画
	ImGuiManager::GetInstance()->Draw();
}

// =====================================================================
// UI構築専用関数（綺麗に整列するように座標指定を追加）
// =====================================================================
void GameScene::UpdateUI() {
	ImGuiManager::GetInstance()->Begin();

	auto satColorOf = [](float v) { return v >= 70.0f ? ImVec4(0, 1, 0, 1) : v >= 40.0f ? ImVec4(1, 1, 0, 1) : ImVec4(1, 0, 0, 1); };
	const char* typeNames[] = {"Empty", "Road", "Residential", "Commercial", "Industrial", "Park"};
	const ImVec4 typeColors[] = {
	    {0.5f, 0.5f, 0.5f, 1},
        {0.6f, 0.6f, 0.6f, 1},
        {0.2f, 0.9f, 0.2f, 1},
        {0.2f, 0.2f, 0.9f, 1},
        {0.9f, 0.6f, 0.1f, 1},
        {0.1f, 0.8f, 0.1f, 1}
    };

	// ① City Dashboard (左上)
	ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(250, 200), ImGuiCond_FirstUseEver);
	ImGui::Begin("City Dashboard");
	float avgSat = cellAutomaton_->GetAverageSatisfaction();
	int pop = cellAutomaton_->GetTotalPopulation();

	ImGui::Text("=== Finance ===");
	ImGui::TextColored(ImVec4(1, 1, 0, 1), "Balance: $%.0f", cityBalance_);
	if (cityBalance_ <= 0.0f)
		ImGui::TextColored(ImVec4(1, 0, 0, 1), "No funds! (財政破綻)");
	ImGui::Separator();

	ImGui::Text("=== Satisfaction ===");
	ImGui::TextColored(satColorOf(avgSat), "Avg: %.0f / 100", avgSat);
	ImGui::ProgressBar(avgSat / 100.0f, ImVec2(-1, 0), "");
	ImGui::Separator();

	ImGui::Text("=== Population ===");
	ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.f, 1), "Total: %d", pop);
	ImGui::Separator();

	ImGui::Checkbox("Enable Simulation", &cellAutomaton_->enableSimulation_);
	ImGui::SliderFloat("Speed (s)", &cellAutomaton_->simInterval_, 0.2f, 3.0f);
	ImGui::End();

	// ② Population Log (左中)
	ImGui::SetNextWindowPos(ImVec2(10, 220), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(250, 180), ImGuiCond_FirstUseEver);
	ImGui::Begin("Population Log");
	const auto& log = cellAutomaton_->GetPopLog();
	if (log.empty())
		ImGui::TextDisabled("No data. Enable Simulation.");
	else {
		ImGui::Text("Turn   Total    Change");
		ImGui::Separator();
		int shown = 0;
		for (int i = (int)log.size() - 1; i >= 0 && shown < 8; --i, ++shown) {
			const PopLogEntry& e = log[i];
			ImVec4 col = e.delta > 0 ? ImVec4(0, 1, 0, 1) : e.delta < 0 ? ImVec4(1, 0.3f, 0.3f, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1);
			ImGui::TextColored(col, "T-%2d   %-6d  %+d", (int)log.size() - i, e.total, e.delta);
		}
	}
	ImGui::End();

	// ③ Income Log (左下)
	ImGui::SetNextWindowPos(ImVec2(10, 410), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(250, 200), ImGuiCond_FirstUseEver);
	ImGui::Begin("Income Log");
	const auto& incLog = cellAutomaton_->GetIncomeLog();
	if (incLog.empty())
		ImGui::TextDisabled("No data.");
	else {
		ImGui::Text("Turn  Com  Upkeep  Net");
		ImGui::Separator();
		int shownInc = 0;
		for (int i = (int)incLog.size() - 1; i >= 0 && shownInc < 8; --i, ++shownInc) {
			const IncomeLogEntry& e = incLog[i];
			ImVec4 col = e.net > 0.0f ? ImVec4(0, 1, 0, 1) : e.net < 0.0f ? ImVec4(1, 0.3f, 0.3f, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1);
			ImGui::TextColored(col, "T-%2d  %-5.0f  %-6.0f  %+.0f", (int)incLog.size() - i, e.commercial, e.maintenance, e.net);
		}
	}
	ImGui::End();

	// ④ Controls / Info (右上)
	ImGui::SetNextWindowPos(ImVec2(970, 10), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(300, 350), ImGuiCond_FirstUseEver);
	ImGui::Begin("Controls & Info");

	ImGui::Text("Selected: ");
	ImGui::SameLine();
	ImGui::TextColored(typeColors[static_cast<int>(currentPlacingType)], "%s", typeNames[static_cast<int>(currentPlacingType)]);

	if (ImGui::CollapsingHeader("Build Costs", ImGuiTreeNodeFlags_DefaultOpen)) {
		CellType types[] = {CellType::ROAD, CellType::RESIDENTIAL, CellType::COMMERCIAL, CellType::INDUSTRIAL, CellType::PARK};
		for (int i = 0; i < 5; ++i) {
			auto cost = cellAutomaton_->GetBuildingCost(types[i]);
			ImGui::TextColored(typeColors[static_cast<int>(types[i])], "%d:%-12s $%.0f", i + 1, typeNames[static_cast<int>(types[i])], cost.buildCost);
		}
		ImGui::Text("0: Remove (free) / Tab: Heatmap");
	}

	if (ImGui::CollapsingHeader("Cell Inspector", ImGuiTreeNodeFlags_DefaultOpen)) {
		int cx, cz;
		cellAutomaton_->GetCursorPosition(cx, cz);
		Cell* cell = cellAutomaton_->GetCell(cx, cz);
		ImGui::Text("Cursor: (%d, %d)", cx, cz);

		if (cell->levelTimer > 0) {
			ImGui::TextColored(ImVec4(0, 1, 0, 1), "Upgrading: %d / 5", cell->levelTimer);
		} else if (cell->levelTimer < 0) {
			ImGui::TextColored(ImVec4(1, 0, 0, 1), "Decaying: %d / -5", cell->levelTimer);
		}

		if (cell && cell->type != CellType::EMPTY) {
			ImGui::TextColored(typeColors[static_cast<int>(cell->type)], "%s (Lv.%d)", typeNames[static_cast<int>(cell->type)], cell->level);

			if (cell->type == CellType::RESIDENTIAL)
				ImGui::Text("Population: %d", cell->population);
			else
				ImGui::Text("Activity: %d", cell->population);

			if (cell->type == CellType::COMMERCIAL || cell->type == CellType::INDUSTRIAL) {
				float eff = cellAutomaton_->GetAgeEfficiency(cell->age);
				ImVec4 effCol = eff >= 0.99f ? ImVec4(0, 1, 0, 1) : eff >= 0.6f ? ImVec4(1, 1, 0, 1) : ImVec4(1, 0.3f, 0.3f, 1);
				ImGui::Text("Age: %d turns", cell->age);
				ImGui::TextColored(effCol, "Efficiency: %.0f%%", eff * 100.0f);
			}

			if (cell->type == CellType::RESIDENTIAL) {
				float sat = cell->satisfaction;
				ImGui::TextColored(satColorOf(sat), "Satisfaction: %.0f", sat);
				ImGui::ProgressBar(sat / 100.0f, ImVec2(-1, 0), "");
			}
		} else {
			ImGui::TextDisabled("(Empty)");
		}
	}
	ImGui::End();

	// ⑤ View Settings (右中)
	ImGui::SetNextWindowPos(ImVec2(970, 370), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(300, 140), ImGuiCond_FirstUseEver);
	ImGui::Begin("View Options");
	bool isHeatmap = (cellAutomaton_->displayMode_ == DisplayMode::Heatmap);
	if (ImGui::RadioButton("Buildings", !isHeatmap))
		cellAutomaton_->displayMode_ = DisplayMode::Normal;
	ImGui::SameLine();
	if (ImGui::RadioButton("Heatmap", isHeatmap))
		cellAutomaton_->displayMode_ = DisplayMode::Heatmap;

	if (isHeatmap) {
		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.1f, 0.8f, 0.1f, 1), "Green : Very good");
		ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.2f, 1), "Yellow: Good");
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1), "White : Neutral / Empty");
		ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1), "Red   : Bad");
		ImGui::TextColored(ImVec4(0.7f, 0.3f, 0.9f, 1), "Purple: Worst (Polluted)");
	}
	ImGui::End();

	// ⑥ Developer Tweaks (右下)
	ImGui::SetNextWindowPos(ImVec2(970, 520), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(300, 250), ImGuiCond_FirstUseEver); // 少し縦幅を広げました
	ImGui::Begin("Developer Tweaks");

	if (ImGui::CollapsingHeader("Building Costs")) {
		ImGui::SliderFloat("Road Cost", &params_.costRoad, 0.0f, 1000.0f);
		ImGui::SliderFloat("House Cost", &params_.costResidential, 0.0f, 2000.0f);
		ImGui::SliderFloat("Commercial Cost", &params_.costCommercial, 0.0f, 2000.0f);
		ImGui::SliderFloat("Industrial Cost", &params_.costIndustrial, 0.0f, 2000.0f);
		ImGui::SliderFloat("Park Cost", &params_.costPark, 0.0f, 1000.0f);
	}

	if (ImGui::CollapsingHeader("Maintenance & Sim")) {
		ImGui::SliderFloat("Com Maint", &params_.maintCommercial, 0.0f, 50.0f);
		ImGui::SliderFloat("Ind Maint", &params_.maintIndustrial, 0.0f, 50.0f);
		ImGui::SliderInt("Age Grace", &params_.ageGrace, 0, 100);
		ImGui::SliderFloat("Age Decay", &params_.ageDecay, 0.01f, 0.5f);
		ImGui::SliderFloat("Min Efficiency", &params_.ageMinEff, 0.0f, 1.0f);
	}

	if (ImGui::Button("Save to JSON"))
		SaveParameters();
	ImGui::End();

	ImGuiManager::GetInstance()->End();
}