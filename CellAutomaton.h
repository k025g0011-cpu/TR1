#pragma once
#include "KamataEngine.h"
#include <unordered_map>
#include <vector>

enum class CellType {
	EMPTY,
	ROAD,
	RESIDENTIAL,
	COMMERCIAL,
	INDUSTRIAL,
	PARK,
};

struct Cell {
	CellType type = CellType::EMPTY;
	int level = 0;
	int population = 0;
	float income = 0.0f;
	KamataEngine::WorldTransform* worldTransform_ = nullptr;
};

// ── 建物コスト定義 ──
struct BuildingCost {
	float buildCost;       // 建設費
	float maintenanceCost; // 毎ターンの維持費
};

// 各CellTypeの建設費・維持費
// 各CellTypeの建設費・維持費（levelが上がるほど維持費も増える）
inline BuildingCost GetBuildingCost(CellType type, int level = 0) {
	float levelMultiplier = 1.0f + level * 0.8f; // レベル0=1倍、1=1.8倍、2=2.6倍、3=3.4倍
	switch (type) {
	case CellType::ROAD:
		return {100.0f, 1.0f * levelMultiplier};
	case CellType::RESIDENTIAL:
		return {300.0f, 2.0f * levelMultiplier};
	case CellType::COMMERCIAL:
		return {500.0f, 4.0f * levelMultiplier};
	case CellType::INDUSTRIAL:
		return {400.0f, 3.0f * levelMultiplier};
	case CellType::PARK:
		return {200.0f, 2.0f * levelMultiplier};
	default:
		return {0.0f, 0.0f};
	}
}

class CellAutomaton {
private:
	static const int GRID_SIZE = 30;
	std::vector<std::vector<Cell>> grid_;

	KamataEngine::Model* cellModel_ = nullptr;
	KamataEngine::Camera* camera_ = nullptr;

	uint32_t groundTexture_ = 0;
	uint32_t cursorTexture_ = 0;
	std::unordered_map<CellType, uint32_t> textureHandles_;

	KamataEngine::WorldTransform cursorWorldTransform_;
	int cursorX_ = 15, cursorZ_ = 15;

	float simTimer_ = 0.0f;

	bool IsAdjacentToRoad(int x, int z);
	int CountNearbyType(int x, int z, CellType type, int radius);
	void SimulateResidential(int x, int z);
	void SimulateCommercial(int x, int z);
	void SimulateIndustrial(int x, int z);
	void RunSimulation();
	void DrawCursor(KamataEngine::PrimitiveDrawer* drawer);

public:
	bool enableSimulation_ = false;
	float simInterval_ = 1.0f;

	CellAutomaton();
	~CellAutomaton();

	void Initialize(KamataEngine::Model* model, KamataEngine::Camera* camera);
	void Update(float deltaTime);
	void Draw(KamataEngine::PrimitiveDrawer* drawer);

	void MoveCursor(int dx, int dz);
	void GetCursorPosition(int& x, int& z) const {
		x = cursorX_;
		z = cursorZ_;
	}

	void PlaceCell(int x, int z, CellType type);
	void PlaceCellAtCursor(CellType type);
	void RemoveCell(int x, int z);
	Cell* GetCell(int x, int z);
	int GetGridSize() const { return GRID_SIZE; }

	int GetTotalPopulation() const;
	float GetTotalIncome() const;
	float GetTotalMaintenance() const; // ★ 維持費合計
};