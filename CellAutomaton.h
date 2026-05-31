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

// ★ 表示モード（発表中に切り替える）
enum class DisplayMode {
	Normal,  // 通常：3Dの建物を描画
	Heatmap, // ヒートマップ：建物を隠し、床タイルを満足度の色で塗る
};

struct Cell {
	CellType type = CellType::EMPTY;
	int level = 0;
	int population = 0;
	float income = 0.0f;
	float satisfaction = 50.0f; // ★ 満足度 0〜100（人口を決める中心指標）
	float influence = 0.0f;     // ★ 町への寄与度 -1.0〜+1.0（ヒートマップ色分け用）
	KamataEngine::WorldTransform* worldTransform_ = nullptr;
};

// ── 建物コスト定義 ──
struct BuildingCost {
	float buildCost;       // 建設費
	float maintenanceCost; // 毎ターンの維持費
};

// 各CellTypeの建設費・維持費
inline BuildingCost GetBuildingCost(CellType type) {
	switch (type) {
	case CellType::ROAD:
		return {100.0f, 5.0f};
	case CellType::RESIDENTIAL:
		return {300.0f, 10.0f};
	case CellType::COMMERCIAL:
		return {500.0f, 20.0f};
	case CellType::INDUSTRIAL:
		return {400.0f, 15.0f};
	case CellType::PARK:
		return {200.0f, 8.0f};
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

	// ★ ヒートマップ用：寄与度の段階別テクスチャ（既存の単色pngを色として流用）
	//    influence が低い→高い の順に並べる（悪い→良い）
	uint32_t heatStrongBad_ = 0; // 強い悪影響
	uint32_t heatBad_ = 0;       // 悪影響
	uint32_t heatNeutral_ = 0;   // 中立
	uint32_t heatGood_ = 0;      // やや好影響
	uint32_t heatVeryGood_ = 0;  // 強い好影響
	uint32_t heatOther_ = 0;     // 住宅以外の建物（黒）
	uint32_t heatEmpty_ = 0;     // 空きマス（白）

	KamataEngine::WorldTransform cursorWorldTransform_;
	int cursorX_ = 15, cursorZ_ = 15;

	float simTimer_ = 0.0f;

	// ★ 満足度伝播用：前ターンの満足度スナップショット
	//    住宅同士が影響し合うとき、走査順に依存しないよう
	//    「前ターンの値」をここに固定しておいて参照する。
	std::vector<std::vector<float>> prevSatisfaction_;

	bool IsAdjacentToRoad(int x, int z);
	int CountNearbyType(int x, int z, CellType type, int radius);
	// ★ 周囲(半径radius)の住宅の、前ターン満足度の平均。住宅がなければ-1を返す。
	float AverageNeighborSatisfaction(int x, int z, int radius);
	void SimulateResidential(int x, int z);
	void SimulateCommercial(int x, int z);
	void SimulateIndustrial(int x, int z);
	void UpdateSatisfaction(int x, int z); // ★ 満足度を更新（近隣住宅の影響を含む）
	void UpdateInfluence(int x, int z);    // ★ 町への寄与度を更新（色分け用）
	void RunSimulation();
	void DrawCursor(KamataEngine::PrimitiveDrawer* drawer);

	void DrawNormal();  // ★ 通常描画（3Dの建物）
	void DrawHeatmap(); // ★ ヒートマップ描画（床タイル）

	uint32_t InfluenceToTexture(const Cell& cell); // ★ 寄与度→段階別テクスチャ

public:
	bool enableSimulation_ = false;
	float simInterval_ = 1.0f;
	DisplayMode displayMode_ = DisplayMode::Normal; // ★ 表示モード

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

	void ToggleDisplayMode() { // ★ 表示モードを切り替え
		displayMode_ = (displayMode_ == DisplayMode::Normal) ? DisplayMode::Heatmap : DisplayMode::Normal;
	}

	void PlaceCell(int x, int z, CellType type);
	void PlaceCellAtCursor(CellType type);
	void RemoveCell(int x, int z);
	Cell* GetCell(int x, int z);
	int GetGridSize() const { return GRID_SIZE; }

	int GetTotalPopulation() const;
	float GetTotalIncome() const;
	float GetTotalMaintenance() const;
	float GetAverageSatisfaction() const; // ★ 平均満足度
};