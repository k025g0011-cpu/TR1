#pragma once
#include "KamataEngine.h"
#include <unordered_map>
#include <vector>

enum class CellType { EMPTY, ROAD, RESIDENTIAL, COMMERCIAL, INDUSTRIAL, PARK };

enum class TrafficState { SMOOTH, MODERATE, CONGESTED };

struct Cell {
	CellType type = CellType::EMPTY;
	int level = 0;
	float development = 0.0f;
	float traffic = 0.0f;
	int population = 0;
	float income = 0.0f;
	KamataEngine::Vector3 position = {0.0f, 0.0f, 0.0f};
	KamataEngine::Vector4 color = {0.2f, 0.2f, 0.2f, 1.0f};
	KamataEngine::WorldTransform* worldTransform_ = nullptr;
};

// ★ スナップショット用：ポインタを持たない軽量データ
struct CellData {
	CellType type = CellType::EMPTY;
	int level = 0;
	float development = 0.0f;
	float traffic = 0.0f;
	int population = 0;
	float income = 0.0f;
	KamataEngine::Vector4 color = {0.2f, 0.2f, 0.2f, 1.0f};
};

struct Snapshot {
	std::vector<std::vector<CellData>> gridData;
	int turn = 0;
};

class CellAutomaton {
private:
	static const int GRID_SIZE = 30;
	std::vector<std::vector<Cell>> grid_;
	std::vector<std::vector<Cell>> nextGrid_;

	float updateTimer_ = 0.0f;

	KamataEngine::Model* cellModel_ = nullptr;
	KamataEngine::Camera* camera_ = nullptr;
	std::unordered_map<CellType, uint32_t> textureHandles_;

	uint32_t roadSmoothTexture_ = 0;
	uint32_t roadModerateTexture_ = 0;
	uint32_t roadCongestedTexture_ = 0;
	uint32_t groundTexture_ = 0;

	// ★ カーソル用テクスチャ・トランスフォーム（建物と分離）
	uint32_t cursorTexture_ = 0;
	KamataEngine::WorldTransform cursorWorldTransform_;

	int cursorX_ = 15;
	int cursorZ_ = 15;

	// ★ タイムライン用
	std::vector<Snapshot> history_;
	int currentSnapshotIndex_ = -1;
	static const int MAX_HISTORY = 500;

	KamataEngine::Vector4 GetColorForType(CellType type, int level);
	std::vector<Cell*> GetNeighbors(int x, int z);

	void UpdateTraffic();
	TrafficState GetTrafficState(float traffic);
	void AddTrafficToNearbyRoads(int x, int z, float amount);

	void ApplyDevelopmentRules(int x, int z);
	void UpdateResidential(int x, int z);
	void UpdateCommercial(int x, int z);
	void UpdateIndustrial(int x, int z);
	void UpdatePark(int x, int z);

	// ★ スナップショット操作
	void SaveSnapshot();
	void RestoreSnapshot(int index);

	// ★ 影響範囲のラインを描画
	void DrawInfluenceLines(KamataEngine::PrimitiveDrawer* drawer);

public:
	float updateInterval_ = 1.0f;
	bool enableAutomaton_ = false;

	// ★ タイムライン自動再生用
	bool isPlaying_ = false;
	float playTimer_ = 0.0f;
	float playInterval_ = 0.5f;

	CellAutomaton();
	~CellAutomaton();

	void Initialize(KamataEngine::Model* model, KamataEngine::Camera* camera);
	void Update(float deltaTime);

	// ★ PrimitiveDrawerを受け取るDraw
	void Draw(KamataEngine::PrimitiveDrawer* drawer);

	void MoveCursor(int dx, int dz);
	void GetCursorPosition(int& x, int& z) const {
		x = cursorX_;
		z = cursorZ_;
	}

	void PlaceCellAtCursor(CellType type);
	void PlaceCell(int x, int z, CellType type);
	void RemoveCell(int x, int z);

	Cell* GetCell(int x, int z);
	int GetGridSize() const { return GRID_SIZE; }

	// ★ タイムライン用パブリックAPI
	void SeekToSnapshot(int index);
	int GetHistorySize() const { return static_cast<int>(history_.size()); }
	int GetCurrentSnapshotIndex() const { return currentSnapshotIndex_; }

	// ★ 初期配置をターン0としてタイムラインに保存
	void SaveInitialSnapshot() { SaveSnapshot(); }
};