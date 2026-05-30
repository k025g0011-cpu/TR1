#define NOMINMAX
#include "CellAutomaton.h"
#include "WorldTransform.h"
#include <cmath>

CellAutomaton::CellAutomaton()
    : roadSmoothTexture_(0), roadModerateTexture_(0), roadCongestedTexture_(0), groundTexture_(0), cursorTexture_(0), cellModel_(nullptr), camera_(nullptr), updateTimer_(0.0f), cursorX_(15),
      cursorZ_(15) {

	grid_.resize(GRID_SIZE, std::vector<Cell>(GRID_SIZE));
	nextGrid_.resize(GRID_SIZE, std::vector<Cell>(GRID_SIZE));

	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			grid_[x][z].position = {static_cast<float>(x), 0.0f, static_cast<float>(z)};
			grid_[x][z].type = CellType::EMPTY;
			grid_[x][z].color = {0.2f, 0.2f, 0.2f, 1.0f};
			grid_[x][z].traffic = 0.0f;
			grid_[x][z].population = 0;
			grid_[x][z].income = 0.0f;
			grid_[x][z].worldTransform_ = new KamataEngine::WorldTransform();
			grid_[x][z].worldTransform_->Initialize();
		}
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

	textureHandles_[CellType::RESIDENTIAL] = KamataEngine::TextureManager::Load("gide1x1.png");
	textureHandles_[CellType::COMMERCIAL] = KamataEngine::TextureManager::Load("blue1x1.png");
	textureHandles_[CellType::INDUSTRIAL] = KamataEngine::TextureManager::Load("black1x1.png");
	textureHandles_[CellType::PARK] = KamataEngine::TextureManager::Load("green1x1.png");

	roadSmoothTexture_ = KamataEngine::TextureManager::Load("green1x1.png");
	roadModerateTexture_ = KamataEngine::TextureManager::Load("yellow1x1.png");
	roadCongestedTexture_ = KamataEngine::TextureManager::Load("red1x1.png");
	groundTexture_ = KamataEngine::TextureManager::Load("ground.png");
	cursorTexture_ = KamataEngine::TextureManager::Load("white1x1.png");

	cursorWorldTransform_.Initialize();
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
			int nx = x + dx, nz = z + dz;
			if (nx >= 0 && nx < GRID_SIZE && nz >= 0 && nz < GRID_SIZE)
				neighbors.push_back(&grid_[nx][nz]);
		}
	}
	return neighbors;
}

TrafficState CellAutomaton::GetTrafficState(float traffic) {
	if (traffic < 0.3f)
		return TrafficState::SMOOTH;
	if (traffic < 0.7f)
		return TrafficState::MODERATE;
	return TrafficState::CONGESTED;
}

void CellAutomaton::AddTrafficToNearbyRoads(int x, int z, float amount) {
	for (int dx = -2; dx <= 2; ++dx) {
		for (int dz = -2; dz <= 2; ++dz) {
			int nx = x + dx, nz = z + dz;
			if (nx >= 0 && nx < GRID_SIZE && nz >= 0 && nz < GRID_SIZE) {
				if (grid_[nx][nz].type == CellType::ROAD) {
					float distance = std::sqrt(static_cast<float>(dx * dx + dz * dz));
					grid_[nx][nz].traffic += amount / (distance + 1.0f);
				}
			}
		}
	}
}

void CellAutomaton::UpdateTraffic() {
	for (int x = 0; x < GRID_SIZE; ++x)
		for (int z = 0; z < GRID_SIZE; ++z)
			if (grid_[x][z].type == CellType::ROAD)
				grid_[x][z].traffic = 0.0f;

	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			Cell& cell = grid_[x][z];
			float tg = 0.0f;
			switch (cell.type) {
			case CellType::RESIDENTIAL:
				tg = 0.1f * (cell.level + 1);
				break;
			case CellType::COMMERCIAL:
				tg = 0.15f * (cell.level + 1);
				break;
			case CellType::INDUSTRIAL:
				tg = 0.12f * (cell.level + 1);
				break;
			default:
				continue;
			}
			AddTrafficToNearbyRoads(x, z, tg);
		}
	}
}

void CellAutomaton::UpdateResidential(int x, int z) {
	Cell& cell = nextGrid_[x][z];
	auto neighbors = GetNeighbors(x, z);
	int roadCount = 0, commercialCount = 0, parkCount = 0;
	float avgTraffic = 0.0f;
	int tsc = 0;
	for (auto* n : neighbors) {
		if (n->type == CellType::ROAD) {
			roadCount++;
			avgTraffic += n->traffic;
			tsc++;
		}
		if (n->type == CellType::COMMERCIAL)
			commercialCount++;
		if (n->type == CellType::PARK)
			parkCount++;
	}
	if (tsc > 0)
		avgTraffic /= tsc;
	if (roadCount > 0) {
		float rate = 0.1f * roadCount;
		if (avgTraffic > 0.7f)
			rate *= 0.3f;
		else if (avgTraffic > 0.3f)
			rate *= 0.7f;
		cell.development += rate;
		if (commercialCount > 0)
			cell.development += 0.05f;
		if (parkCount > 0)
			cell.development += 0.05f;
	}
	if (cell.development >= 1.0f && cell.level < 3) {
		cell.level++;
		cell.development = 0.0f;
		cell.population = 100 * (cell.level + 1);
		cell.income = cell.population * 0.05f;
	}
}

void CellAutomaton::UpdateCommercial(int x, int z) {
	Cell& cell = nextGrid_[x][z];
	auto neighbors = GetNeighbors(x, z);
	int roadCount = 0, residentialCount = 0;
	float avgTraffic = 0.0f;
	int tsc = 0;
	for (auto* n : neighbors) {
		if (n->type == CellType::ROAD) {
			roadCount++;
			avgTraffic += n->traffic;
			tsc++;
		}
		if (n->type == CellType::RESIDENTIAL)
			residentialCount++;
	}
	if (tsc > 0)
		avgTraffic /= tsc;
	if (roadCount > 0) {
		float rate = 0.08f * roadCount;
		if (avgTraffic > 0.7f)
			rate *= 0.3f;
		else if (avgTraffic > 0.3f)
			rate *= 0.7f;
		cell.development += rate;
		if (residentialCount >= 2)
			cell.development += 0.1f;
	}
	if (cell.development >= 1.0f && cell.level < 3) {
		cell.level++;
		cell.development = 0.0f;
		cell.population = 50 * (cell.level + 1);
		cell.income = cell.population * 0.1f;
	}
}

void CellAutomaton::UpdateIndustrial(int x, int z) {
	Cell& cell = nextGrid_[x][z];
	auto neighbors = GetNeighbors(x, z);
	int roadCount = 0, residentialNearby = 0;
	float avgTraffic = 0.0f;
	int tsc = 0;
	for (auto* n : neighbors) {
		if (n->type == CellType::ROAD) {
			roadCount++;
			avgTraffic += n->traffic;
			tsc++;
		}
		if (n->type == CellType::RESIDENTIAL)
			residentialNearby++;
	}
	if (tsc > 0)
		avgTraffic /= tsc;
	if (roadCount > 0) {
		float rate = 0.1f * roadCount;
		if (avgTraffic > 0.7f)
			rate *= 0.2f;
		else if (avgTraffic > 0.3f)
			rate *= 0.6f;
		cell.development += rate;
		if (residentialNearby > 0)
			cell.development -= 0.02f * residentialNearby;
	}
	if (cell.development >= 1.0f && cell.level < 3) {
		cell.level++;
		cell.development = 0.0f;
		cell.population = 80 * (cell.level + 1);
		cell.income = cell.population * 0.08f;
	}
}

void CellAutomaton::UpdatePark(int x, int z) {
	Cell& cell = nextGrid_[x][z];
	auto neighbors = GetNeighbors(x, z);
	int roadCount = 0, residentialCount = 0;
	for (auto* n : neighbors) {
		if (n->type == CellType::ROAD)
			roadCount++;
		if (n->type == CellType::RESIDENTIAL)
			residentialCount++;
	}
	if (roadCount > 0) {
		cell.development += 0.08f * roadCount;
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
	case CellType::PARK:
		UpdatePark(x, z);
		break;
	default:
		break;
	}
	next.color = GetColorForType(next.type, next.level);
}

void CellAutomaton::Update(float deltaTime) {
	UpdateTraffic();

	// タイムライン自動再生
	if (isPlaying_) {
		if (GetHistorySize() > 0) {
			playTimer_ += deltaTime;
			if (playTimer_ >= playInterval_) {
				playTimer_ = 0.0f;
				int nextIdx = currentSnapshotIndex_ + 1;
				if (nextIdx < GetHistorySize()) {
					SeekToSnapshot(nextIdx);
				} else {
					isPlaying_ = false;
				}
			}
		} else {
			isPlaying_ = false;
		}
		return;
	}

	if (!enableAutomaton_)
		return;

	updateTimer_ += deltaTime;
	if (updateTimer_ >= updateInterval_) {
		updateTimer_ = 0.0f;
		for (int x = 0; x < GRID_SIZE; ++x)
			for (int z = 0; z < GRID_SIZE; ++z)
				ApplyDevelopmentRules(x, z);

		// ★ worldTransform_ポインタを保持しながらデータだけコピー
		for (int x = 0; x < GRID_SIZE; ++x) {
			for (int z = 0; z < GRID_SIZE; ++z) {
				KamataEngine::WorldTransform* wt = grid_[x][z].worldTransform_;
				grid_[x][z] = nextGrid_[x][z];
				grid_[x][z].worldTransform_ = wt;
			}
		}
		SaveSnapshot();
	}
}

void CellAutomaton::MoveCursor(int dx, int dz) {
	cursorX_ = std::max(0, std::min(GRID_SIZE - 1, cursorX_ + dx));
	cursorZ_ = std::max(0, std::min(GRID_SIZE - 1, cursorZ_ + dz));
}

void CellAutomaton::PlaceCellAtCursor(CellType type) { PlaceCell(cursorX_, cursorZ_, type); }

void CellAutomaton::DrawInfluenceLines(KamataEngine::PrimitiveDrawer* drawer) {
	if (!drawer)
		return;

	Cell* cursorCell = GetCell(cursorX_, cursorZ_);
	if (!cursorCell || cursorCell->type == CellType::EMPTY)
		return;

	const KamataEngine::Vector4 lineColor = {1.0f, 1.0f, 0.0f, 1.0f};
	const float y = 0.1f;

	for (int dx = -1; dx <= 1; ++dx) {
		for (int dz = -1; dz <= 1; ++dz) {
			if (dx == 0 && dz == 0)
				continue;
			int nx = cursorX_ + dx, nz = cursorZ_ + dz;
			if (nx < 0 || nx >= GRID_SIZE || nz < 0 || nz >= GRID_SIZE)
				continue;

			float x0 = static_cast<float>(nx), x1 = x0 + 1.0f;
			float z0 = static_cast<float>(nz), z1 = z0 + 1.0f;
			drawer->DrawLine3d({x0, y, z0}, {x1, y, z0}, lineColor);
			drawer->DrawLine3d({x1, y, z0}, {x1, y, z1}, lineColor);
			drawer->DrawLine3d({x1, y, z1}, {x0, y, z1}, lineColor);
			drawer->DrawLine3d({x0, y, z1}, {x0, y, z0}, lineColor);
		}
	}

	const KamataEngine::Vector4 cursorLineColor = {1.0f, 1.0f, 1.0f, 1.0f};
	float cx0 = static_cast<float>(cursorX_), cx1 = cx0 + 1.0f;
	float cz0 = static_cast<float>(cursorZ_), cz1 = cz0 + 1.0f;
	drawer->DrawLine3d({cx0, y, cz0}, {cx1, y, cz0}, cursorLineColor);
	drawer->DrawLine3d({cx1, y, cz0}, {cx1, y, cz1}, cursorLineColor);
	drawer->DrawLine3d({cx1, y, cz1}, {cx0, y, cz1}, cursorLineColor);
	drawer->DrawLine3d({cx0, y, cz1}, {cx0, y, cz0}, cursorLineColor);
}

void CellAutomaton::Draw(KamataEngine::PrimitiveDrawer* drawer) {
	if (!cellModel_ || !camera_)
		return;

	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			Cell& cell = grid_[x][z];
			KamataEngine::WorldTransform* transform = cell.worldTransform_;

			transform->translation_.x = static_cast<float>(x) + 0.5f;
			transform->translation_.z = static_cast<float>(z) + 0.5f;

			if (cell.type == CellType::EMPTY) {
				transform->translation_.y = 0.0f;
				transform->scale_ = {0.5f, 0.02f, 0.5f};
				WorldTransformUpdate(*transform);
				cellModel_->Draw(*transform, *camera_, groundTexture_);
				continue;
			}

			float height = 0.5f, scaleXZ = 0.5f;
			switch (cell.type) {
			case CellType::ROAD:
				height = 0.05f;
				scaleXZ = 0.5f;
				break;
			case CellType::RESIDENTIAL:
				height = 0.5f + (cell.level * 0.5f);
				scaleXZ = 0.45f;
				break;
			case CellType::COMMERCIAL:
				height = 0.8f + (cell.level * 0.7f);
				scaleXZ = 0.4f;
				break;
			case CellType::INDUSTRIAL:
				height = 0.6f + (cell.level * 0.5f);
				scaleXZ = 0.45f;
				break;
			case CellType::PARK:
				height = 0.1f;
				scaleXZ = 0.48f;
				break;
			default:
				height = 0.5f;
				scaleXZ = 0.5f;
				break;
			}

			transform->translation_.y = height;
			transform->scale_ = {scaleXZ, height, scaleXZ};
			WorldTransformUpdate(*transform);

			uint32_t texture = roadSmoothTexture_;
			if (cell.type == CellType::ROAD) {
				switch (GetTrafficState(cell.traffic)) {
				case TrafficState::SMOOTH:
					texture = roadSmoothTexture_;
					break;
				case TrafficState::MODERATE:
					texture = roadModerateTexture_;
					break;
				case TrafficState::CONGESTED:
					texture = roadCongestedTexture_;
					break;
				}
			} else {
				texture = textureHandles_[cell.type];
			}
			cellModel_->Draw(*transform, *camera_, texture);
		}
	}

	// カーソル専用トランスフォームで描画
	{
		cursorWorldTransform_.translation_.x = static_cast<float>(cursorX_) + 0.5f;
		cursorWorldTransform_.translation_.z = static_cast<float>(cursorZ_) + 0.5f;
		cursorWorldTransform_.translation_.y = 0.04f;
		cursorWorldTransform_.scale_ = {0.5f, 0.04f, 0.5f};
		WorldTransformUpdate(cursorWorldTransform_);
		cellModel_->Draw(cursorWorldTransform_, *camera_, cursorTexture_);
	}

	DrawInfluenceLines(drawer);
}

void CellAutomaton::PlaceCell(int x, int z, CellType type) {
	if (x >= 0 && x < GRID_SIZE && z >= 0 && z < GRID_SIZE) {
		grid_[x][z].type = type;
		grid_[x][z].level = 0;
		grid_[x][z].development = 0.0f;
		grid_[x][z].traffic = 0.0f;
		grid_[x][z].population = 0;
		grid_[x][z].income = 0.0f;
		grid_[x][z].color = GetColorForType(type, 0);
	}
}

void CellAutomaton::RemoveCell(int x, int z) {
	if (x >= 0 && x < GRID_SIZE && z >= 0 && z < GRID_SIZE) {
		grid_[x][z].type = CellType::EMPTY;
		grid_[x][z].level = 0;
		grid_[x][z].development = 0.0f;
		grid_[x][z].traffic = 0.0f;
		grid_[x][z].population = 0;
		grid_[x][z].income = 0.0f;
		grid_[x][z].color = {0.2f, 0.2f, 0.2f, 1.0f};
	}
}

Cell* CellAutomaton::GetCell(int x, int z) {
	if (x >= 0 && x < GRID_SIZE && z >= 0 && z < GRID_SIZE)
		return &grid_[x][z];
	return nullptr;
}

void CellAutomaton::SaveSnapshot() {
	if ((int)history_.size() >= MAX_HISTORY) {
		history_.erase(history_.begin());
		// ★ 先頭削除分だけインデックスをずらす（0未満にはしない）
		currentSnapshotIndex_ = std::max(0, currentSnapshotIndex_ - 1);
	}

	Snapshot snap;
	snap.turn = (int)history_.size();
	snap.gridData.resize(GRID_SIZE, std::vector<CellData>(GRID_SIZE));

	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			const Cell& src = grid_[x][z];
			CellData& dst = snap.gridData[x][z];
			dst.type = src.type;
			dst.level = src.level;
			dst.development = src.development;
			dst.traffic = src.traffic;
			dst.population = src.population;
			dst.income = src.income;
			dst.color = src.color;
		}
	}
	history_.push_back(std::move(snap));
	currentSnapshotIndex_ = (int)history_.size() - 1;
}

void CellAutomaton::RestoreSnapshot(int index) {
	if (index < 0 || index >= (int)history_.size())
		return;
	const Snapshot& snap = history_[index];
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			const CellData& src = snap.gridData[x][z];
			Cell& dst = grid_[x][z];
			dst.type = src.type;
			dst.level = src.level;
			dst.development = src.development;
			dst.traffic = src.traffic;
			dst.population = src.population;
			dst.income = src.income;
			dst.color = src.color;
		}
	}
}

void CellAutomaton::SeekToSnapshot(int index) {
	if (index < 0 || index >= (int)history_.size())
		return;
	currentSnapshotIndex_ = index;
	RestoreSnapshot(index);
}