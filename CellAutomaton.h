#pragma once
#include "KamataEngine.h"
#include <unordered_map>
#include <vector>

enum class CellType { EMPTY, ROAD, RESIDENTIAL, COMMERCIAL, INDUSTRIAL, PARK };

enum class DisplayMode { Normal, Heatmap };

struct Cell {
	CellType type = CellType::EMPTY;
	int level = 0;
	int population = 0;
	float income = 0.0f;        // 将来用に残す（今は表示しない）
	float satisfaction = 50.0f; // 満足度 0〜100
	float influence = 0.0f;     // ヒートマップ色分け用 -1〜+1
	int age = 0;                // ★ 築年数（経過ターン数）。老朽化に使う。
	KamataEngine::WorldTransform* worldTransform_ = nullptr;
};

// ── 建設費＋維持費 ──
// ★ maintCost: 1ターンごとにかかる固定の維持費。
//    ここの数値を変えるだけで全体のバランス調整ができる。
struct BuildingCost {
	float buildCost;
	float maintCost;
};

inline BuildingCost GetBuildingCost(CellType type) {
	switch (type) {
	case CellType::ROAD:
		return {100.0f, 0.0f};
	case CellType::RESIDENTIAL:
		return {300.0f, 0.0f};
	case CellType::COMMERCIAL:
		return {500.0f, 20.0f};
	case CellType::INDUSTRIAL:
		return {400.0f, 15.0f};
	case CellType::PARK:
		return {200.0f, 0.0f};
	default:
		return {0.0f, 0.0f};
	}
}

// ★ 人口ログ（1ターンごとの変化を記録）
struct PopLogEntry {
	int total; // そのターンの総人口
	int delta; // 前ターンからの増減
};

// ★ 収入ログ（1ターンごとの収入を記録）
struct IncomeLogEntry {
	float total;       // そのターンの総収入（維持費を引く前）
	float commercial;  // 商業の合計収入
	float industrial;  // 工業の合計収入
	float maintenance; // 維持費の合計
	float net;         // 純収益 = total - maintenance
};

class CellAutomaton {
private:
	static const int GRID_SIZE = 30;
	std::vector<std::vector<Cell>> grid_;

	KamataEngine::Model* cellModel_ = nullptr;
	KamataEngine::Camera* camera_ = nullptr;

	uint32_t groundTexture_ = 0;
	uint32_t cursorTexture_ = 0;
	std::unordered_map<CellType, uint32_t> textureHandles_;

	// ヒートマップ用テクスチャ
	uint32_t heatStrongBad_ = 0;
	uint32_t heatBad_ = 0;
	uint32_t heatNeutral_ = 0;
	uint32_t heatGood_ = 0;
	uint32_t heatVeryGood_ = 0;
	uint32_t heatOther_ = 0;
	uint32_t heatEmpty_ = 0;

	KamataEngine::WorldTransform cursorWorldTransform_;
	int cursorX_ = 15, cursorZ_ = 15;

	float simTimer_ = 0.0f;

	// ★ 収入：pendingIncome_ は未回収分（GameSceneが取りに来てリセット）
	//          lastTurnIncome_ は表示用（リセットしない）
	float pendingIncome_ = 0.0f;
	float lastTurnIncome_ = 0.0f;
	float lastTurnMaintenance_ = 0.0f; // ★ 表示用：直近ターンの維持費合計
	float lastTurnNet_ = 0.0f;         // ★ 表示用：直近ターンの純収益

	// 満足度伝播用スナップショット
	std::vector<std::vector<float>> prevSatisfaction_;

	// ★ 人口ログ（最大20件）
	std::vector<PopLogEntry> popLog_;
	int prevTotalPop_ = 0;
	static const int MAX_LOG = 20;

	// ★ 収入ログ（最大20件）
	std::vector<IncomeLogEntry> incomeLog_;

	bool IsAdjacentToRoad(int x, int z);
	int CountNearbyType(int x, int z, CellType type, int radius);
	float AverageNeighborSatisfaction(int x, int z, int radius);

	// ★ 老朽化：築年数から収入効率(0〜1)を求める
	//    ここの3つの定数を変えるだけで老朽化の速さ・きつさを調整できる。
	static constexpr int AGE_GRACE = 20;         // この築年数までは効率100%（新築期間）
	static constexpr float AGE_DECAY = 0.02f;   // 猶予を過ぎた後、1ターンごとに下がる効率
	static constexpr float AGE_MIN_EFF = 0.20f; // 効率の下限（これ以下には落ちない）
	float AgeEfficiency(int age) const;

	void SimulateResidential(int x, int z);
	void SimulateCommercial(int x, int z);
	void SimulateIndustrial(int x, int z);
	void UpdateSatisfaction(int x, int z);
	void UpdateInfluence(int x, int z);
	void RunSimulation();

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

	void Initialize(KamataEngine::Model* model, KamataEngine::Camera* camera);
	void Update(float deltaTime);
	void Draw(KamataEngine::PrimitiveDrawer* drawer);

	void MoveCursor(int dx, int dz);
	void GetCursorPosition(int& x, int& z) const {
		x = cursorX_;
		z = cursorZ_;
	}
	void ToggleDisplayMode() { displayMode_ = (displayMode_ == DisplayMode::Normal) ? DisplayMode::Heatmap : DisplayMode::Normal; }

	void PlaceCell(int x, int z, CellType type);
	void PlaceCellAtCursor(CellType type);
	void RemoveCell(int x, int z);
	Cell* GetCell(int x, int z);
	int GetGridSize() const { return GRID_SIZE; }

	int GetTotalPopulation() const;
	float GetAverageSatisfaction() const;

	// ★ 表示用：直近ターンに発生した総収入（リセットしない）
	float GetLastTurnIncome() const { return lastTurnIncome_; }
	// ★ 表示用：直近ターンの維持費合計／純収益
	float GetLastTurnMaintenance() const { return lastTurnMaintenance_; }
	float GetLastTurnNet() const { return lastTurnNet_; }

	// ★ 築年数から収入効率(0〜1)を返す（Cell Info表示用）
	float GetAgeEfficiency(int age) const { return AgeEfficiency(age); }
	// ★ 回収用：未回収の収入を返し、内部はゼロに戻す（二重加算防止）
	float CollectIncome() {
		float v = pendingIncome_;
		pendingIncome_ = 0.0f;
		return v;
	}

	// ★ 人口ログ取得
	const std::vector<PopLogEntry>& GetPopLog() const { return popLog_; }

	// ★ 収入ログ取得
	const std::vector<IncomeLogEntry>& GetIncomeLog() const { return incomeLog_; }
};