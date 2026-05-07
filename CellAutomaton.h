#pragma once
#include "KamataEngine.h"
#include <unordered_map>
#include <vector>

enum class CellType { EMPTY, ROAD, RESIDENTIAL, COMMERCIAL, INDUSTRIAL, PARK };

struct Cell {
	CellType type = CellType::EMPTY;
	int level = 0;
	float development = 0.0f;
	KamataEngine::Vector3 position;
	KamataEngine::Vector4 color;
	KamataEngine::WorldTransform* worldTransform_ = nullptr;
};

class CellAutomaton {
private:
	static const int GRID_SIZE = 20;
	std::vector<std::vector<Cell>> grid_;
	std::vector<std::vector<Cell>> nextGrid_;

	float updateTimer_ = 0.0f;

	KamataEngine::Model* cellModel_ = nullptr;
	KamataEngine::Camera* camera_ = nullptr;
	std::unordered_map<CellType, uint32_t> textureHandles_;

	int cursorX_ = 10;
	int cursorZ_ = 10;

	KamataEngine::Vector4 GetColorForType(CellType type, int level);
	std::vector<Cell*> GetNeighbors(int x, int z);

	void ApplyDevelopmentRules(int x, int z);
	void UpdateResidential(int x, int z);
	void UpdateCommercial(int x, int z);
	void UpdateIndustrial(int x, int z);
	void UpdatePark(int x, int z); 

public:
	float updateInterval_ = 1.0f;
	bool enableAutomaton_ = false;

	CellAutomaton();
	~CellAutomaton();

	void Initialize(KamataEngine::Model* model, KamataEngine::Camera* camera);
	void Update(float deltaTime);
	void Draw();

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
};