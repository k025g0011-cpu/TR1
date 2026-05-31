#define NOMINMAX
#include "CellAutomaton.h"
#include "WorldTransform.h"
#include <algorithm>

CellAutomaton::CellAutomaton() {
	grid_.resize(GRID_SIZE, std::vector<Cell>(GRID_SIZE));
	for (int x = 0; x < GRID_SIZE; ++x)
		for (int z = 0; z < GRID_SIZE; ++z) {
			grid_[x][z].worldTransform_ = new KamataEngine::WorldTransform();
			grid_[x][z].worldTransform_->Initialize();
		}
}

CellAutomaton::~CellAutomaton() {
	for (int x = 0; x < GRID_SIZE; ++x)
		for (int z = 0; z < GRID_SIZE; ++z)
			delete grid_[x][z].worldTransform_;
}

void CellAutomaton::Initialize(KamataEngine::Model* model, KamataEngine::Camera* camera) {
	cellModel_ = model;
	camera_ = camera;

	groundTexture_ = KamataEngine::TextureManager::Load("ground.png");
	cursorTexture_ = KamataEngine::TextureManager::Load("white1x1.png");

	textureHandles_[CellType::ROAD] = KamataEngine::TextureManager::Load("white1x1.png");
	textureHandles_[CellType::RESIDENTIAL] = KamataEngine::TextureManager::Load("gide1x1.png");
	textureHandles_[CellType::COMMERCIAL] = KamataEngine::TextureManager::Load("blue1x1.png");
	textureHandles_[CellType::INDUSTRIAL] = KamataEngine::TextureManager::Load("black1x1.png");
	textureHandles_[CellType::PARK] = KamataEngine::TextureManager::Load("green1x1.png");

	cursorWorldTransform_.Initialize();
}

// ══════════════════════════════════════
// 住民シミュレーション
// ══════════════════════════════════════

// 上下左右4方向のどこかに道路があるか
bool CellAutomaton::IsAdjacentToRoad(int x, int z) {
	const int dx[] = {0, 0, 1, -1};
	const int dz[] = {1, -1, 0, 0};
	for (int i = 0; i < 4; ++i) {
		int nx = x + dx[i], nz = z + dz[i];
		if (nx < 0 || nx >= GRID_SIZE || nz < 0 || nz >= GRID_SIZE)
			continue;
		if (grid_[nx][nz].type == CellType::ROAD)
			return true;
	}
	return false;
}

// 指定セルから radius マス以内に type の建物が何個あるか
int CellAutomaton::CountNearbyType(int x, int z, CellType type, int radius) {
	int count = 0;
	for (int dx = -radius; dx <= radius; ++dx)
		for (int dz = -radius; dz <= radius; ++dz) {
			if (dx == 0 && dz == 0)
				continue;
			int nx = x + dx, nz = z + dz;
			if (nx < 0 || nx >= GRID_SIZE || nz < 0 || nz >= GRID_SIZE)
				continue;
			if (grid_[nx][nz].type == type)
				count++;
		}
	return count;
}

// 住宅の人口を更新
// 毎ターン「目標人口」に向かって少しずつ増減する
void CellAutomaton::SimulateResidential(int x, int z) {
	Cell& cell = grid_[x][z];

	// 道路なし → 人口を徐々に0へ
	if (!IsAdjacentToRoad(x, z)) {
		cell.population = std::max(0, cell.population - 5);
		cell.income = cell.population * 0.20f;
		return;
	}

	// 目標人口を計算
	int targetPop = 50 * (cell.level + 1);
	targetPop += CountNearbyType(x, z, CellType::COMMERCIAL, 3) * 20;
	targetPop -= CountNearbyType(x, z, CellType::INDUSTRIAL, 3) * 15;
	targetPop += CountNearbyType(x, z, CellType::PARK, 2) * 10;
	targetPop = std::max(0, targetPop);

	// 目標に向かって毎ターン±10ずつ近づく
	if (cell.population < targetPop)
		cell.population = std::min(targetPop, cell.population + 10);
	else if (cell.population > targetPop)
		cell.population = std::max(targetPop, cell.population - 10);

	cell.income = cell.population * 0.20f;
}

// 商業施設の収入を更新
void CellAutomaton::SimulateCommercial(int x, int z) {
	Cell& cell = grid_[x][z];

	if (!IsAdjacentToRoad(x, z)) {
		cell.population = std::max(0, cell.population - 3);
		cell.income = cell.population * 0.40f;
		return;
	}

	int targetWorkers = 30 * (cell.level + 1);
	targetWorkers += CountNearbyType(x, z, CellType::RESIDENTIAL, 4) * 5;
	targetWorkers = std::max(0, targetWorkers);

	if (cell.population < targetWorkers)
		cell.population = std::min(targetWorkers, cell.population + 5);
	else if (cell.population > targetWorkers)
		cell.population = std::max(targetWorkers, cell.population - 5);

	cell.income = cell.population * 0.40f;
}

// 工業施設の収入を更新
void CellAutomaton::SimulateIndustrial(int x, int z) {
	Cell& cell = grid_[x][z];

	if (!IsAdjacentToRoad(x, z)) {
		cell.population = std::max(0, cell.population - 3);
		cell.income = cell.population * 0.30f;
		return;
	}

	int targetWorkers = 40 * (cell.level + 1);
	targetWorkers -= CountNearbyType(x, z, CellType::RESIDENTIAL, 3) * 8;
	targetWorkers = std::max(0, targetWorkers);

	if (cell.population < targetWorkers)
		cell.population = std::min(targetWorkers, cell.population + 5);
	else if (cell.population > targetWorkers)
		cell.population = std::max(targetWorkers, cell.population - 5);

	cell.income = cell.population * 0.30f;
}

// 全セルのシミュレーションを1ステップ実行
void CellAutomaton::RunSimulation() {
	for (int x = 0; x < GRID_SIZE; ++x)
		for (int z = 0; z < GRID_SIZE; ++z) {
			switch (grid_[x][z].type) {
			case CellType::RESIDENTIAL:
				SimulateResidential(x, z);
				break;
			case CellType::COMMERCIAL:
				SimulateCommercial(x, z);
				break;
			case CellType::INDUSTRIAL:
				SimulateIndustrial(x, z);
				break;
			default:
				break;
			}
		}
}

// ══════════════════════════════════════
// 統計
// ══════════════════════════════════════

int CellAutomaton::GetTotalPopulation() const {
	int total = 0;
	for (int x = 0; x < GRID_SIZE; ++x)
		for (int z = 0; z < GRID_SIZE; ++z)
			total += grid_[x][z].population;
	return total;
}

float CellAutomaton::GetTotalIncome() const {
	float total = 0.0f;
	for (int x = 0; x < GRID_SIZE; ++x)
		for (int z = 0; z < GRID_SIZE; ++z)
			total += grid_[x][z].income;
	return total;
}

// ★ 全建物の維持費合計
float CellAutomaton::GetTotalMaintenance() const {
	float total = 0.0f;
	for (int x = 0; x < GRID_SIZE; ++x)
		for (int z = 0; z < GRID_SIZE; ++z)
			if (grid_[x][z].type != CellType::EMPTY)
				total += GetBuildingCost(grid_[x][z].type).maintenanceCost;
	return total;
}

// ══════════════════════════════════════
// Update / Draw
// ══════════════════════════════════════

void CellAutomaton::Update(float deltaTime) {
	if (!enableSimulation_)
		return;

	simTimer_ += deltaTime;
	if (simTimer_ >= simInterval_) {
		simTimer_ = 0.0f;
		RunSimulation();
	}
}

void CellAutomaton::MoveCursor(int dx, int dz) {
	cursorX_ = std::max(0, std::min(GRID_SIZE - 1, cursorX_ + dx));
	cursorZ_ = std::max(0, std::min(GRID_SIZE - 1, cursorZ_ + dz));
}

void CellAutomaton::PlaceCell(int x, int z, CellType type) {
	if (x < 0 || x >= GRID_SIZE || z < 0 || z >= GRID_SIZE)
		return;
	grid_[x][z].type = type;
	grid_[x][z].level = 0;
	grid_[x][z].population = 0;
	grid_[x][z].income = 0.0f;
}

void CellAutomaton::PlaceCellAtCursor(CellType type) { PlaceCell(cursorX_, cursorZ_, type); }

void CellAutomaton::RemoveCell(int x, int z) { PlaceCell(x, z, CellType::EMPTY); }

Cell* CellAutomaton::GetCell(int x, int z) {
	if (x < 0 || x >= GRID_SIZE || z < 0 || z >= GRID_SIZE)
		return nullptr;
	return &grid_[x][z];
}

void CellAutomaton::DrawCursor(KamataEngine::PrimitiveDrawer* drawer) {
	if (!drawer)
		return;
	const float y = 0.1f;

	// カーソル：白枠
	float cx0 = static_cast<float>(cursorX_), cx1 = cx0 + 1.0f;
	float cz0 = static_cast<float>(cursorZ_), cz1 = cz0 + 1.0f;
	KamataEngine::Vector4 white = {1, 1, 1, 1};
	drawer->DrawLine3d({cx0, y, cz0}, {cx1, y, cz0}, white);
	drawer->DrawLine3d({cx1, y, cz0}, {cx1, y, cz1}, white);
	drawer->DrawLine3d({cx1, y, cz1}, {cx0, y, cz1}, white);
	drawer->DrawLine3d({cx0, y, cz1}, {cx0, y, cz0}, white);

	// 建物があるとき：隣接8セルを黄色枠
	Cell* c = GetCell(cursorX_, cursorZ_);
	if (!c || c->type == CellType::EMPTY)
		return;

	KamataEngine::Vector4 yellow = {1, 1, 0, 1};
	for (int dx = -1; dx <= 1; ++dx)
		for (int dz = -1; dz <= 1; ++dz) {
			if (dx == 0 && dz == 0)
				continue;
			int nx = cursorX_ + dx, nz = cursorZ_ + dz;
			if (nx < 0 || nx >= GRID_SIZE || nz < 0 || nz >= GRID_SIZE)
				continue;
			float x0 = static_cast<float>(nx), x1 = x0 + 1.0f;
			float z0 = static_cast<float>(nz), z1 = z0 + 1.0f;
			drawer->DrawLine3d({x0, y, z0}, {x1, y, z0}, yellow);
			drawer->DrawLine3d({x1, y, z0}, {x1, y, z1}, yellow);
			drawer->DrawLine3d({x1, y, z1}, {x0, y, z1}, yellow);
			drawer->DrawLine3d({x0, y, z1}, {x0, y, z0}, yellow);
		}
}

void CellAutomaton::Draw(KamataEngine::PrimitiveDrawer* drawer) {
	if (!cellModel_ || !camera_)
		return;

	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			Cell& cell = grid_[x][z];
			KamataEngine::WorldTransform* t = cell.worldTransform_;
			t->translation_.x = static_cast<float>(x) + 0.5f;
			t->translation_.z = static_cast<float>(z) + 0.5f;

			if (cell.type == CellType::EMPTY) {
				t->translation_.y = 0.0f;
				t->scale_ = {0.5f, 0.02f, 0.5f};
				WorldTransformUpdate(*t);
				cellModel_->Draw(*t, *camera_, groundTexture_);
				continue;
			}

			float height = 0.5f, scaleXZ = 0.45f;
			switch (cell.type) {
			case CellType::ROAD:
				height = 0.05f;
				scaleXZ = 0.5f;
				break;
			case CellType::RESIDENTIAL:
				height = 0.6f + cell.level * 0.5f;
				scaleXZ = 0.44f;
				break;
			case CellType::COMMERCIAL:
				height = 0.9f + cell.level * 0.7f;
				scaleXZ = 0.40f;
				break;
			case CellType::INDUSTRIAL:
				height = 0.7f + cell.level * 0.5f;
				scaleXZ = 0.44f;
				break;
			case CellType::PARK:
				height = 0.1f;
				scaleXZ = 0.48f;
				break;
			default:
				break;
			}

			t->translation_.y = height;
			t->scale_ = {scaleXZ, height, scaleXZ};
			WorldTransformUpdate(*t);
			cellModel_->Draw(*t, *camera_, textureHandles_[cell.type]);
		}
	}

	// カーソル
	cursorWorldTransform_.translation_.x = static_cast<float>(cursorX_) + 0.5f;
	cursorWorldTransform_.translation_.z = static_cast<float>(cursorZ_) + 0.5f;
	cursorWorldTransform_.translation_.y = 0.04f;
	cursorWorldTransform_.scale_ = {0.5f, 0.04f, 0.5f};
	WorldTransformUpdate(cursorWorldTransform_);
	cellModel_->Draw(cursorWorldTransform_, *camera_, cursorTexture_);

	DrawCursor(drawer);
}