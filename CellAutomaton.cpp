#define NOMINMAX
#include "CellAutomaton.h"
#include "WorldTransform.h"
#include <algorithm>

using namespace KamataEngine;

// =====================================================================
// 初期化・解放・設定
// =====================================================================
CellAutomaton::CellAutomaton() {
	grid_.resize(GRID_SIZE, std::vector<Cell>(GRID_SIZE));
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			grid_[x][z].worldTransform_ = new WorldTransform();
			grid_[x][z].worldTransform_->Initialize();
		}
	}
	prevSatisfaction_.resize(GRID_SIZE, std::vector<float>(GRID_SIZE, 50.0f));
}

CellAutomaton::~CellAutomaton() {
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			delete grid_[x][z].worldTransform_;
		}
	}
}

void CellAutomaton::Initialize(Model* model, Camera* camera, const GameParameters* params) {
	cellModel_ = model;
	camera_ = camera;
	params_ = params;

	groundTexture_ = TextureManager::Load("ground.png");
	cursorTexture_ = TextureManager::Load("white1x1.png");

	textureHandles_[CellType::ROAD] = TextureManager::Load("gray1x1.png");
	textureHandles_[CellType::RESIDENTIAL] = TextureManager::Load("gide1x1.png");
	textureHandles_[CellType::COMMERCIAL] = TextureManager::Load("blue1x1.png");
	textureHandles_[CellType::INDUSTRIAL] = TextureManager::Load("black1x1.png");

	heatStrongBad_ = TextureManager::Load("darkRed1x1.png");
	heatBad_ = TextureManager::Load("red1x1.png");
	heatNeutral_ = TextureManager::Load("white1x1.png");
	heatGood_ = TextureManager::Load("green1x1.png");
	heatVeryGood_ = TextureManager::Load("darkGreen1x1.png");
	heatOther_ = TextureManager::Load("black1x1.png");
	heatEmpty_ = TextureManager::Load("white1x1.png");

	cursorWorldTransform_.Initialize();
}

BuildingCost CellAutomaton::GetBuildingCost(CellType type) const {
	if (!params_)
		return {0.0f, 0.0f};
	switch (type) {
	case CellType::ROAD:
		return {params_->costRoad, 0.0f};
	case CellType::RESIDENTIAL:
		return {params_->costResidential, 0.0f};
	case CellType::COMMERCIAL:
		return {params_->costCommercial, params_->maintCommercial};
	case CellType::INDUSTRIAL:
		return {params_->costIndustrial, params_->maintIndustrial};
	default:
		return {0.0f, 0.0f};
	}
}

// =====================================================================
// シミュレーション論理 サブルーチン
// =====================================================================
bool CellAutomaton::IsAdjacentToRoad(int x, int z) {
	const int dx[] = {0, 0, 1, -1}, dz[] = {1, -1, 0, 0};
	for (int i = 0; i < 4; ++i) {
		int nx = x + dx[i], nz = z + dz[i];
		if (nx >= 0 && nx < GRID_SIZE && nz >= 0 && nz < GRID_SIZE && grid_[nx][nz].type == CellType::ROAD)
			return true;
	}
	return false;
}

int CellAutomaton::CountNearbyType(int x, int z, CellType type, int radius) {
	int count = 0;
	for (int dx = -radius; dx <= radius; ++dx) {
		for (int dz = -radius; dz <= radius; ++dz) {
			if (dx == 0 && dz == 0)
				continue;
			int nx = x + dx, nz = z + dz;
			if (nx >= 0 && nx < GRID_SIZE && nz >= 0 && nz < GRID_SIZE && grid_[nx][nz].type == type)
				count++;
		}
	}
	return count;
}

float CellAutomaton::AgeEfficiency(int age) const {
	if (!params_ || age <= params_->ageGrace)
		return 1.0f;
	float eff = 1.0f - (age - params_->ageGrace) * params_->ageDecay;
	return std::max(params_->ageMinEff, eff);
}

float CellAutomaton::AverageNeighborSatisfaction(int x, int z, int radius) {
	float total = 0.0f;
	int count = 0;
	for (int dx = -radius; dx <= radius; ++dx) {
		for (int dz = -radius; dz <= radius; ++dz) {
			if (dx == 0 && dz == 0)
				continue;
			int nx = x + dx, nz = z + dz;
			if (nx >= 0 && nx < GRID_SIZE && nz >= 0 && nz < GRID_SIZE && grid_[nx][nz].type == CellType::RESIDENTIAL) {
				total += prevSatisfaction_[nx][nz];
				count++;
			}
		}
	}
	return (count > 0) ? total / count : -1.0f;
}

void CellAutomaton::UpdateSatisfaction(int x, int z) {
	Cell& cell = grid_[x][z];
	if (cell.type != CellType::RESIDENTIAL)
		return;

	float s = 50.0f;
	s += IsAdjacentToRoad(x, z) ? 20.0f : -50.0f;
	s += CountNearbyType(x, z, CellType::COMMERCIAL, 3) * 15.0f;
	s -= CountNearbyType(x, z, CellType::INDUSTRIAL, 3) * 30.0f;

	float neighborAvg = AverageNeighborSatisfaction(x, z, 1);
	if (neighborAvg >= 0.0f) {
		s += (neighborAvg - s) * 0.3f;
	}
	cell.satisfaction = std::max(0.0f, std::min(100.0f, s));
}

void CellAutomaton::SimulateResidential(int x, int z) {
	Cell& cell = grid_[x][z];
	int maxPop = 100 * (cell.level + 1);
	float sat = cell.satisfaction;

	if (sat >= 70.f) {
		int gain = static_cast<int>((sat - 70.f) / 10.f) + 1;
		cell.population = std::min(maxPop, cell.population + gain);
	} else if (sat < 40.f && sat >= 20.f) {
		int loss = static_cast<int>((40.f - sat) / 10.f) + 1;
		cell.population = std::max(0, cell.population - loss);
	} else if (sat < 20.f) {
		int loss = static_cast<int>((20.f - sat) / 4.f) + 3;
		cell.population = std::max(0, cell.population - loss);
	}
	cell.income = 0.0f;
}

void CellAutomaton::SimulateCommercial(int x, int z) {
	Cell& cell = grid_[x][z];
	if (!IsAdjacentToRoad(x, z)) {
		cell.population = 0;
		cell.income = 0.0f;
		return;
	}

	int customers = 0;
	for (int dx = -4; dx <= 4; ++dx) {
		for (int dz = -4; dz <= 4; ++dz) {
			if (dx == 0 && dz == 0)
				continue;
			int nx = x + dx, nz = z + dz;
			if (nx >= 0 && nx < GRID_SIZE && nz >= 0 && nz < GRID_SIZE && grid_[nx][nz].type == CellType::RESIDENTIAL)
				customers += grid_[nx][nz].population;
		}
	}

	float capacity = 200.0f * (cell.level + 1);
	int factories = CountNearbyType(x, z, CellType::INDUSTRIAL, 4);
	capacity *= (1.0f + std::min(1.5f, factories * 0.5f));

	int served = std::min(customers, static_cast<int>(capacity));
	cell.population = served;
	cell.income = served * 0.15f * AgeEfficiency(cell.age);
}

void CellAutomaton::SimulateIndustrial(int x, int z) {
	Cell& cell = grid_[x][z];
	if (!IsAdjacentToRoad(x, z)) {
		cell.population = 0;
		cell.income = 0.0f;
		return;
	}

	int baseOutput = 30 * (cell.level + 1);
	cell.population = baseOutput;
	cell.income = baseOutput * 0.4f * AgeEfficiency(cell.age);
}

void CellAutomaton::UpdateBuildingLevels() {
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			Cell& cell = grid_[x][z];

			// 道路・空き地は対象外
			if (cell.type == CellType::EMPTY || cell.type == CellType::ROAD) {
				cell.levelTimer = 0;
				continue;
			}

			bool isGood = false, isBad = false;

			// ==========================================
			// 発表用：4要素に絞った明確なレベルアップ条件
			// ==========================================
			if (cell.type == CellType::RESIDENTIAL) {
				// ① 住宅：近くに商業がある（満足度75以上）と育ち、工業がある（30未満）と衰退する
				if (cell.satisfaction >= 75.0f)
					isGood = true;
				else if (cell.satisfaction < 30.0f)
					isBad = true;
			} else if (cell.type == CellType::COMMERCIAL) {
				// ② 商業：客（人口）がキャパの80%以上来ると育ち、20%未満だと衰退する
				float capacity = 200.0f * (cell.level + 1);
				if (cell.population >= capacity * 0.8f)
					isGood = true;
				else if (cell.population < capacity * 0.2f)
					isBad = true;
			} else if (cell.type == CellType::INDUSTRIAL) {
				// ③ 工業：新しくて効率が良い（80%以上）と育ち、老朽化（50%未満）すると衰退する
				if (AgeEfficiency(cell.age) >= 0.8f)
					isGood = true;
				else if (AgeEfficiency(cell.age) < 0.5f)
					isBad = true;
			}

			// ==========================================
			// タイマー更新（すべて「3ターン連続」で統一）
			// ==========================================
			if (isGood) {
				if (++cell.levelTimer >= 3) {
					if (cell.level < 2) {
						cell.level++;
						cell.levelTimer = 0;
						if (cell.type == CellType::RESIDENTIAL)
							cell.population += 20;
					} else {
						cell.levelTimer = 3;
					}
				}
			} else if (isBad) {
				if (--cell.levelTimer <= -3) {
					if (cell.level > 0) {
						cell.level--;
						cell.levelTimer = 0;
					} else {
						cell.levelTimer = -3;
					}
				}
			} else {
				if (cell.levelTimer > 0)
					cell.levelTimer--;
				else if (cell.levelTimer < 0)
					cell.levelTimer++;
			}
		}
	}
}

void CellAutomaton::UpdateInfluence(int x, int z) {
	Cell& cell = grid_[x][z];
	if (cell.type == CellType::RESIDENTIAL) {
		cell.influence = std::max(-1.0f, std::min(1.0f, (cell.satisfaction - 50.0f) / 50.0f));
	} else {
		cell.influence = 0.0f;
	}
}

// =====================================================================
// メインシミュレーションループ
// =====================================================================
void CellAutomaton::RunSimulation() {
	for (int x = 0; x < GRID_SIZE; ++x)
		for (int z = 0; z < GRID_SIZE; ++z)
			prevSatisfaction_[x][z] = grid_[x][z].satisfaction;

	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			if (grid_[x][z].type != CellType::EMPTY)
				grid_[x][z].age++;
			UpdateSatisfaction(x, z);
		}
	}

	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			if (grid_[x][z].type == CellType::RESIDENTIAL)
				SimulateResidential(x, z);
			else if (grid_[x][z].type == CellType::COMMERCIAL)
				SimulateCommercial(x, z);
			else if (grid_[x][z].type == CellType::INDUSTRIAL)
				SimulateIndustrial(x, z);
		}
	}

	UpdateBuildingLevels();
	for (int x = 0; x < GRID_SIZE; ++x)
		for (int z = 0; z < GRID_SIZE; ++z)
			UpdateInfluence(x, z);

	float income = 0.0f, comIncome = 0.0f, indIncome = 0.0f, maintenance = 0.0f;
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			Cell& c = grid_[x][z];
			income += c.income;
			if (c.type == CellType::COMMERCIAL)
				comIncome += c.income;
			else if (c.type == CellType::INDUSTRIAL)
				indIncome += c.income;
			maintenance += GetBuildingCost(c.type).maintCost;
		}
	}
	float net = income - maintenance;
	lastTurnIncome_ = income;
	lastTurnMaintenance_ = maintenance;
	lastTurnNet_ = net;
	pendingIncome_ += net;

	incomeLog_.push_back({income, comIncome, indIncome, maintenance, net});
	if (incomeLog_.size() > MAX_LOG)
		incomeLog_.erase(incomeLog_.begin());

	int currentPop = GetTotalPopulation();
	popLog_.push_back({currentPop, currentPop - prevTotalPop_});
	prevTotalPop_ = currentPop;
	if (popLog_.size() > MAX_LOG)
		popLog_.erase(popLog_.begin());
}

void CellAutomaton::Update(float deltaTime) {
	if (!enableSimulation_)
		return;
	simTimer_ += deltaTime;
	if (simTimer_ >= simInterval_) {
		simTimer_ = 0.0f;
		RunSimulation();
	}
}

// =====================================================================
// インターフェース / 操作系
// =====================================================================
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
	grid_[x][z].satisfaction = 50.0f;
	grid_[x][z].influence = 0.0f;
	grid_[x][z].age = 0;
	grid_[x][z].levelTimer = 0;
}

void CellAutomaton::RemoveCell(int x, int z) { PlaceCell(x, z, CellType::EMPTY); }

Cell* CellAutomaton::GetCell(int x, int z) {
	if (x < 0 || x >= GRID_SIZE || z < 0 || z >= GRID_SIZE)
		return nullptr;
	return &grid_[x][z];
}

int CellAutomaton::GetTotalPopulation() const {
	int total = 0;
	for (int x = 0; x < GRID_SIZE; ++x)
		for (int z = 0; z < GRID_SIZE; ++z)
			if (grid_[x][z].type == CellType::RESIDENTIAL)
				total += grid_[x][z].population;
	return total;
}

float CellAutomaton::GetAverageSatisfaction() const {
	float total = 0.0f;
	int count = 0;
	for (int x = 0; x < GRID_SIZE; ++x)
		for (int z = 0; z < GRID_SIZE; ++z)
			if (grid_[x][z].type == CellType::RESIDENTIAL) {
				total += grid_[x][z].satisfaction;
				count++;
			}
	return count > 0 ? total / count : 0.0f;
}

float CellAutomaton::CollectIncome() {
	float v = pendingIncome_;
	pendingIncome_ = 0.0f;
	return v;
}

// =====================================================================
// 描画処理系
// =====================================================================
void CellAutomaton::DrawCursor(PrimitiveDrawer* drawer) {
	if (!drawer)
		return;
	const float y = 0.1f;
	float cx0 = static_cast<float>(cursorX_), cx1 = cx0 + 1.0f;
	float cz0 = static_cast<float>(cursorZ_), cz1 = cz0 + 1.0f;
	Vector4 white = {1, 1, 1, 1}, yellow = {1, 1, 0, 1};

	drawer->DrawLine3d({cx0, y, cz0}, {cx1, y, cz0}, white);
	drawer->DrawLine3d({cx1, y, cz0}, {cx1, y, cz1}, white);
	drawer->DrawLine3d({cx1, y, cz1}, {cx0, y, cz1}, white);
	drawer->DrawLine3d({cx0, y, cz1}, {cx0, y, cz0}, white);

	Cell* c = GetCell(cursorX_, cursorZ_);
	if (!c || c->type == CellType::EMPTY)
		return;

	for (int dx = -1; dx <= 1; ++dx) {
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
}

uint32_t CellAutomaton::InfluenceToTexture(const Cell& cell) {
	if (cell.type == CellType::EMPTY)
		return heatEmpty_;
	if (cell.type != CellType::RESIDENTIAL)
		return heatOther_;

	float v = cell.influence;
	if (v <= -0.6f)
		return heatStrongBad_;
	else if (v < -0.1f)
		return heatBad_;
	else if (v <= 0.3f)
		return heatNeutral_;
	else if (v <= 0.65f)
		return heatGood_;
	else
		return heatVeryGood_;
}

void CellAutomaton::DrawNormal() {
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			Cell& cell = grid_[x][z];
			WorldTransform* t = cell.worldTransform_;
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
			default:
				break;
			}
			t->translation_.y = height;
			t->scale_ = {scaleXZ, height, scaleXZ};
			WorldTransformUpdate(*t);
			cellModel_->Draw(*t, *camera_, textureHandles_[cell.type]);
		}
	}
}

void CellAutomaton::DrawHeatmap() {
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			Cell& cell = grid_[x][z];
			WorldTransform* t = cell.worldTransform_;
			t->translation_.x = static_cast<float>(x) + 0.5f;
			t->translation_.z = static_cast<float>(z) + 0.5f;
			t->translation_.y = 0.02f;
			t->scale_ = {0.5f, 0.02f, 0.5f};

			WorldTransformUpdate(*t);
			cellModel_->Draw(*t, *camera_, InfluenceToTexture(cell));
		}
	}
}

void CellAutomaton::Draw(KamataEngine::PrimitiveDrawer* drawer) {
	if (!cellModel_ || !camera_)
		return;

	if (displayMode_ == DisplayMode::Heatmap)
		DrawHeatmap();
	else
		DrawNormal();

	cursorWorldTransform_.translation_.x = static_cast<float>(cursorX_) + 0.5f;
	cursorWorldTransform_.translation_.z = static_cast<float>(cursorZ_) + 0.5f;
	cursorWorldTransform_.translation_.y = 0.04f;
	cursorWorldTransform_.scale_ = {0.5f, 0.04f, 0.5f};
	WorldTransformUpdate(cursorWorldTransform_);
	cellModel_->Draw(cursorWorldTransform_, *camera_, cursorTexture_);

	DrawCursor(drawer);
}