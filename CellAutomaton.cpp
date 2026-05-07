#include "CellAutomaton.h"
#include "WorldTransform.h"
#include <cmath>

CellAutomaton::CellAutomaton() {
	grid_.resize(GRID_SIZE, std::vector<Cell>(GRID_SIZE));
	nextGrid_.resize(GRID_SIZE, std::vector<Cell>(GRID_SIZE));

	// グリッドの初期化
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			grid_[x][z].position = {static_cast<float>(x), 0.0f, static_cast<float>(z)};
			grid_[x][z].type = CellType::EMPTY;
			grid_[x][z].color = {0.2f, 0.2f, 0.2f, 1.0f};

			grid_[x][z].worldTransform_ = new KamataEngine::WorldTransform();
			grid_[x][z].worldTransform_->Initialize();
		}
	}
}

CellAutomaton::~CellAutomaton() {
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			delete grid_[x][z].worldTransform_;
		}
	}
}

void CellAutomaton::Initialize(KamataEngine::Model* model, KamataEngine::Camera* camera) {
	cellModel_ = model;
	camera_ = camera;

	// 各建物タイプ用のテクスチャを読み込む

	textureHandles_[CellType::ROAD] = KamataEngine::TextureManager::Load("gray1x1.png");
	textureHandles_[CellType::RESIDENTIAL] = KamataEngine::TextureManager::Load("white1x1.png");
	textureHandles_[CellType::COMMERCIAL] = KamataEngine::TextureManager::Load("white1x1.png");
	textureHandles_[CellType::INDUSTRIAL] = KamataEngine::TextureManager::Load("white1x1.png");
	textureHandles_[CellType::PARK] = KamataEngine::TextureManager::Load("green1x1.png");
}

KamataEngine::Vector4 CellAutomaton::GetColorForType(CellType type, int level) {
	float intensity = 0.4f + (level * 0.2f);

	switch (type) {
	case CellType::EMPTY:
		return {0.2f, 0.2f, 0.2f, 1.0f};
	case CellType::ROAD:
		return {0.3f, 0.3f, 0.3f, 1.0f};
	case CellType::RESIDENTIAL:
		return {0.2f * intensity, 0.8f * intensity, 0.2f * intensity, 1.0f};
	case CellType::COMMERCIAL:
		return {0.2f * intensity, 0.2f * intensity, 0.8f * intensity, 1.0f};
	case CellType::INDUSTRIAL:
		return {0.8f * intensity, 0.7f * intensity, 0.2f * intensity, 1.0f};
	case CellType::PARK:
		return {0.1f, 0.6f, 0.1f, 1.0f};
	default:
		return {1.0f, 1.0f, 1.0f, 1.0f};
	}
}

std::vector<Cell*> CellAutomaton::GetNeighbors(int x, int z) {
	std::vector<Cell*> neighbors;

	for (int dx = -1; dx <= 1; ++dx) {
		for (int dz = -1; dz <= 1; ++dz) {
			if (dx == 0 && dz == 0)
				continue;

			int nx = x + dx;
			int nz = z + dz;

			if (nx >= 0 && nx < GRID_SIZE && nz >= 0 && nz < GRID_SIZE) {
				neighbors.push_back(&grid_[nx][nz]);
			}
		}
	}

	return neighbors;
}

void CellAutomaton::UpdateResidential(int x, int z) {
	Cell& cell = nextGrid_[x][z];
	auto neighbors = GetNeighbors(x, z);

	int roadCount = 0;
	int commercialCount = 0;
	int parkCount = 0;

	for (auto* neighbor : neighbors) {
		if (neighbor->type == CellType::ROAD)
			roadCount++;
		if (neighbor->type == CellType::COMMERCIAL)
			commercialCount++;
		if (neighbor->type == CellType::PARK)
			parkCount++;
	}

	// 道路が多いほど発展（道路1つにつき+0.1、最大8つで+0.8）
	if (roadCount > 0) {
		cell.development += 0.1f * roadCount;

		// ボーナス：商業施設や公園が近くにあるとさらに発展
		if (commercialCount > 0)
			cell.development += 0.05f;
		if (parkCount > 0)
			cell.development += 0.05f;
	}

	if (cell.development >= 1.0f && cell.level < 3) {
		cell.level++;
		cell.development = 0.0f;
	}
}

void CellAutomaton::UpdateCommercial(int x, int z) {
	Cell& cell = nextGrid_[x][z];
	auto neighbors = GetNeighbors(x, z);

	int roadCount = 0;
	int residentialCount = 0;

	for (auto* neighbor : neighbors) {
		if (neighbor->type == CellType::ROAD)
			roadCount++;
		if (neighbor->type == CellType::RESIDENTIAL)
			residentialCount++;
	}

	// 道路が多いほど発展
	if (roadCount > 0) {
		cell.development += 0.08f * roadCount;

		// ボーナス：住宅が近くにあると客が来る
		if (residentialCount >= 2)
			cell.development += 0.1f;
	}

	if (cell.development >= 1.0f && cell.level < 3) {
		cell.level++;
		cell.development = 0.0f;
	}
}

void CellAutomaton::UpdateIndustrial(int x, int z) {
	Cell& cell = nextGrid_[x][z];
	auto neighbors = GetNeighbors(x, z);

	int roadCount = 0;
	int residentialNearby = 0;

	for (auto* neighbor : neighbors) {
		if (neighbor->type == CellType::ROAD)
			roadCount++;
		if (neighbor->type == CellType::RESIDENTIAL)
			residentialNearby++;
	}

	// 道路が多いほど発展（物流に必要）
	if (roadCount > 0) {
		cell.development += 0.1f * roadCount;

		// ペナルティ：住宅が近いと苦情で発展が遅い
		if (residentialNearby > 0)
			cell.development -= 0.02f * residentialNearby;
	}

	if (cell.development >= 1.0f && cell.level < 3) {
		cell.level++;
		cell.development = 0.0f;
	}
}

void CellAutomaton::UpdatePark(int x, int z) {
	Cell& cell = nextGrid_[x][z];
	auto neighbors = GetNeighbors(x, z);

	int roadCount = 0;
	int residentialCount = 0;

	for (auto* neighbor : neighbors) {
		if (neighbor->type == CellType::ROAD)
			roadCount++;
		if (neighbor->type == CellType::RESIDENTIAL)
			residentialCount++;
	}

	// 道路が多いほどアクセスが良くなり発展
	if (roadCount > 0) {
		cell.development += 0.08f * roadCount;

		// ボーナス：住宅が近くにあると利用者が増える
		if (residentialCount > 0)
			cell.development += 0.05f * residentialCount;
	}

	if (cell.development >= 1.0f && cell.level < 3) {
		cell.level++;
		cell.development = 0.0f;
	}
}


void CellAutomaton::ApplyDevelopmentRules(int x, int z) {
	Cell& current = grid_[x][z];
	Cell& next = nextGrid_[x][z];

	next = current;

	switch (current.type) {
	case CellType::RESIDENTIAL:
		UpdateResidential(x, z);
		break;
	case CellType::COMMERCIAL:
		UpdateCommercial(x, z);
		break;
	case CellType::INDUSTRIAL:
		UpdateIndustrial(x, z);
		break;
	case CellType::PARK: // ← 追加
		UpdatePark(x, z);
		break;
	default:
		break;
	}

	next.color = GetColorForType(next.type, next.level);
}
void CellAutomaton::Update(float deltaTime) {
	// セルオートマトン機能が有効な場合のみ更新
	if (!enableAutomaton_)
		return;

	updateTimer_ += deltaTime;

	if (updateTimer_ >= updateInterval_) {
		updateTimer_ = 0.0f;

		for (int x = 0; x < GRID_SIZE; ++x) {
			for (int z = 0; z < GRID_SIZE; ++z) {
				ApplyDevelopmentRules(x, z);
			}
		}

		grid_ = nextGrid_;
	}
}

void CellAutomaton::MoveCursor(int dx, int dz) {
	cursorX_ += dx;
	cursorZ_ += dz;

	// 範囲制限
	if (cursorX_ < 0)
		cursorX_ = 0;
	if (cursorX_ >= GRID_SIZE)
		cursorX_ = GRID_SIZE - 1;
	if (cursorZ_ < 0)
		cursorZ_ = 0;
	if (cursorZ_ >= GRID_SIZE)
		cursorZ_ = GRID_SIZE - 1;
}

void CellAutomaton::PlaceCellAtCursor(CellType type) { PlaceCell(cursorX_, cursorZ_, type); }


void CellAutomaton::Draw() {
	if (!cellModel_ || !camera_)
		return;

	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			Cell& cell = grid_[x][z];

			if (cell.type == CellType::EMPTY)
				continue;

			KamataEngine::WorldTransform* transform = cell.worldTransform_;

			// マスの中心に配置
			transform->translation_.x = static_cast<float>(x) + 0.5f;
			transform->translation_.z = static_cast<float>(z) + 0.5f;

			// タイプとレベルに応じて形状を変える
			float height = 0.5f;
			float scaleXZ = 0.5f;

			switch (cell.type) {
			case CellType::ROAD:
				height = 0.05f;
				scaleXZ = 0.5f;
				break;
			case CellType::RESIDENTIAL:
				height = 0.5f + (cell.level * 0.3f);
				scaleXZ = 0.5f;
				break;
			case CellType::COMMERCIAL:
				height = 0.8f + (cell.level * 0.4f);
				scaleXZ = 0.5f;
				break;
			case CellType::INDUSTRIAL:
				height = 0.6f + (cell.level * 0.3f);
				scaleXZ = 0.5f;
				break;
			case CellType::PARK:
				height = 0.1f;
				scaleXZ = 0.5f;
				break;
			default:
				break;
			}

			transform->translation_.y = height;
			transform->scale_ = {scaleXZ, height, scaleXZ};
			WorldTransformUpdate(*transform);

			// 建物タイプに応じたテクスチャを使用
			uint32_t texture = textureHandles_[cell.type];
			cellModel_->Draw(*transform, *camera_, texture);
		}
	}
}
void CellAutomaton::PlaceCell(int x, int z, CellType type) {
	if (x >= 0 && x < GRID_SIZE && z >= 0 && z < GRID_SIZE) {
		grid_[x][z].type = type;
		grid_[x][z].level = 0;
		grid_[x][z].development = 0.0f;
		grid_[x][z].color = GetColorForType(type, 0);
	}
}

void CellAutomaton::RemoveCell(int x, int z) {
	if (x >= 0 && x < GRID_SIZE && z >= 0 && z < GRID_SIZE) {
		grid_[x][z].type = CellType::EMPTY;
		grid_[x][z].level = 0;
		grid_[x][z].development = 0.0f;
		grid_[x][z].color = {0.2f, 0.2f, 0.2f, 1.0f};
	}
}

Cell* CellAutomaton::GetCell(int x, int z) {
	if (x >= 0 && x < GRID_SIZE && z >= 0 && z < GRID_SIZE) {
		return &grid_[x][z];
	}
	return nullptr;
}