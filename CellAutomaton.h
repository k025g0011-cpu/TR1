#pragma once
#include "GameParameters.h"
#include "KamataEngine.h"
#include <unordered_map>
#include <vector>

// 街を構成する4つの要素（+空き地）
enum class CellType { EMPTY, ROAD, RESIDENTIAL, COMMERCIAL, INDUSTRIAL };
enum class DisplayMode { Normal, Heatmap };

struct Cell {
	CellType type = CellType::EMPTY;
	int level = 0;
	int population = 0;
	float income = 0.0f;
	float satisfaction = 50.0f;
	float influence = 0.0f;
	int age = 0;
	int levelTimer = 0; // レベルアップ判定用の連続ターンカウンター
	KamataEngine::WorldTransform* worldTransform_ = nullptr;
};

struct BuildingCost {
	float buildCost;
	float maintCost;
};

struct PopLogEntry {
	int total;
	int delta;
};
struct IncomeLogEntry {
	float total;
	float commercial;
	float industrial;
	float maintenance;
	float net;
};

class CellAutomaton {
private:
	// ── 1. システム・パラメータ ──
	const GameParameters* params_ = nullptr;
	static const int GRID_SIZE = 30;
	std::vector<std::vector<Cell>> grid_;

	// ── 2. グラフィックス関連 ──
	KamataEngine::Model* cellModel_ = nullptr;
	KamataEngine::Camera* camera_ = nullptr;
	uint32_t groundTexture_ = 0, cursorTexture_ = 0;
	std::unordered_map<CellType, uint32_t> textureHandles_;
	uint32_t heatStrongBad_ = 0, heatBad_ = 0, heatNeutral_ = 0, heatGood_ = 0, heatVeryGood_ = 0, heatOther_ = 0, heatEmpty_ = 0;
	KamataEngine::WorldTransform cursorWorldTransform_;
	int cursorX_ = 15, cursorZ_ = 15;

	// ── 3. シミュレーション管理変数 ──
	float simTimer_ = 0.0f;
	float pendingIncome_ = 0.0f, lastTurnIncome_ = 0.0f, lastTurnMaintenance_ = 0.0f, lastTurnNet_ = 0.0f;
	std::vector<std::vector<float>> prevSatisfaction_;
	std::vector<PopLogEntry> popLog_;
	std::vector<IncomeLogEntry> incomeLog_;
	int prevTotalPop_ = 0;
	static const int MAX_LOG = 20;

	// ── 4. 内部シミュレーション処理群 ──
	bool IsAdjacentToRoad(int x, int z);
	int CountNearbyType(int x, int z, CellType type, int radius);
	float AverageNeighborSatisfaction(int x, int z, int radius);
	float AgeEfficiency(int age) const;

	void SimulateResidential(int x, int z);
	void SimulateCommercial(int x, int z);
	void SimulateIndustrial(int x, int z);
	void UpdateSatisfaction(int x, int z);
	void UpdateInfluence(int x, int z);
	void UpdateBuildingLevels();
	void RunSimulation();

	// ── 5. 描画処理群 ──
	void DrawCursor(KamataEngine::PrimitiveDrawer* drawer);
	void DrawNormal();
	void DrawHeatmap();
	uint32_t InfluenceToTexture(const Cell& cell);

public:
	bool enableSimulation_ = false;
	float simInterval_ = 1.0f;
	DisplayMode displayMode_ = DisplayMode::Normal;

	CellAutomaton();
	~CellAutomaton();

	void Initialize(KamataEngine::Model* model, KamataEngine::Camera* camera, const GameParameters* params);
	void Update(float deltaTime);
	void Draw(KamataEngine::PrimitiveDrawer* drawer);

	// ── インターフェース ──
	void MoveCursor(int dx, int dz);
	void GetCursorPosition(int& x, int& z) const {
		x = cursorX_;
		z = cursorZ_;
	}
	void ToggleDisplayMode() { displayMode_ = (displayMode_ == DisplayMode::Normal) ? DisplayMode::Heatmap : DisplayMode::Normal; }

	void PlaceCell(int x, int z, CellType type);
	void RemoveCell(int x, int z);
	Cell* GetCell(int x, int z);

	int GetTotalPopulation() const;
	float GetAverageSatisfaction() const;
	float GetLastTurnIncome() const { return lastTurnIncome_; }
	float GetLastTurnMaintenance() const { return lastTurnMaintenance_; }
	float GetLastTurnNet() const { return lastTurnNet_; }
	float GetAgeEfficiency(int age) const { return AgeEfficiency(age); }
	float CollectIncome();
	BuildingCost GetBuildingCost(CellType type) const;

	const std::vector<PopLogEntry>& GetPopLog() const { return popLog_; }
	const std::vector<IncomeLogEntry>& GetIncomeLog() const { return incomeLog_; }
};