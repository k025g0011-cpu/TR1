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

	// ★ 満足度スナップショット用バッファ（伝播計算で参照する）
	prevSatisfaction_.resize(GRID_SIZE, std::vector<float>(GRID_SIZE, 50.0f));
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

	textureHandles_[CellType::ROAD] = KamataEngine::TextureManager::Load("gray1x1.png");
	textureHandles_[CellType::RESIDENTIAL] = KamataEngine::TextureManager::Load("gide1x1.png");
	textureHandles_[CellType::COMMERCIAL] = KamataEngine::TextureManager::Load("blue1x1.png");
	textureHandles_[CellType::INDUSTRIAL] = KamataEngine::TextureManager::Load("black1x1.png");
	textureHandles_[CellType::PARK] = KamataEngine::TextureManager::Load("green1x1.png");

	// ★ ヒートマップ用テクスチャ（既存の単色pngを色として流用）
	//    住宅：悪い→良い＝紫 → 赤 → 白 → 黄緑 → 緑
	//    住宅以外の建物（道路・商業・工業・公園）：黒
	//    空きマス：白
	//    → 住宅の満足度だけがくっきり浮かび上がる。
	heatStrongBad_ = KamataEngine::TextureManager::Load("darkRed1x1.png"); // 強い悪影響
	heatBad_ = KamataEngine::TextureManager::Load("red1x1.png");          // 悪影響
	heatNeutral_ = KamataEngine::TextureManager::Load("white1x1.png");    // 中立
	heatGood_ = KamataEngine::TextureManager::Load("green1x1.png");      // やや好影響
	heatVeryGood_ = KamataEngine::TextureManager::Load("darkGreen1x1.png");   // 強い好影響
	heatOther_ = KamataEngine::TextureManager::Load("black1x1.png");      // 住宅以外の建物（黒）
	heatEmpty_ = KamataEngine::TextureManager::Load("white1x1.png");      // 空きマス（白）

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

// ══════════════════════════════════════
// ★ 満足度の計算（住宅のみ）
//
// すべての立地条件を「満足度」という1つの指標に集約する。
// 住宅の人口は、この満足度から決まる（SimulateResidential）。
//
//   1段目（立地）:
//     ベース            : 50
//     道路接続あり      : +20  / なし : -50（孤立は致命的）
//     公園が近い        : +15/個（半径2）
//     商業が近い        : +8/個（半径3） 便利さ
//     工業が近い        : -20/個（半径3）公害
//   2段目（伝播）:
//     近隣住宅の前ターン満足度の平均に、差の30%だけ引き寄せられる。
//     → 良い住宅街は周りも良くなり、荒れた街は周りも下がる。
//        数ターンかけて色が移り変わるのが見える。
// ══════════════════════════════════════
// ★ 周囲(半径radius)の住宅の、前ターン満足度の平均
//    伝播計算は必ず「前ターンの値(prevSatisfaction_)」を読む。
//    こうすると走査順に依存せず、変化がきれいに波及する。
//    近くに住宅が1つもなければ -1.0 を返す（呼び出し側で「近所なし」と判定）。
float CellAutomaton::AverageNeighborSatisfaction(int x, int z, int radius) {
	float total = 0.0f;
	int count = 0;
	for (int dx = -radius; dx <= radius; ++dx)
		for (int dz = -radius; dz <= radius; ++dz) {
			if (dx == 0 && dz == 0)
				continue;
			int nx = x + dx, nz = z + dz;
			if (nx < 0 || nx >= GRID_SIZE || nz < 0 || nz >= GRID_SIZE)
				continue;
			if (grid_[nx][nz].type == CellType::RESIDENTIAL) {
				total += prevSatisfaction_[nx][nz]; // ★ 前ターンの値
				count++;
			}
		}
	return count > 0 ? total / count : -1.0f;
}

void CellAutomaton::UpdateSatisfaction(int x, int z) {
	Cell& cell = grid_[x][z];

	if (cell.type != CellType::RESIDENTIAL)
		return;

	// ── 1段目：立地条件から「ベース満足度」を出す（従来通り）──
	float s = 50.0f; // ベース

	// 道路接続が最重要
	s += IsAdjacentToRoad(x, z) ? 20.0f : -50.0f;

	// 周辺環境
	s += CountNearbyType(x, z, CellType::PARK, 2) * 15.0f;       // 公園：快適さ
	s += CountNearbyType(x, z, CellType::COMMERCIAL, 3) * 8.0f;  // 商業：便利さ
	s -= CountNearbyType(x, z, CellType::INDUSTRIAL, 3) * 20.0f; // 工業：公害

	// ── 2段目：近隣住宅からの伝播 ──
	// 周りの住宅が良い街なら自分も少し上がり、荒れていれば下がる（ご近所効果）。
	// ベースとの差の 30% を取り込む（中くらいの伝播）。
	const float spreadFactor = 0.3f;
	float neighborAvg = AverageNeighborSatisfaction(x, z, 1);
	if (neighborAvg >= 0.0f) { // 近所に住宅があるときだけ
		s += (neighborAvg - s) * spreadFactor;
	}

	// 0〜100にクランプ
	cell.satisfaction = std::max(0.0f, std::min(100.0f, s));
}

// ══════════════════════════════════════
// ★ 町への寄与度の計算（ヒートマップ色分け用）
//
// influence を -1.0〜+1.0 に正規化して各セルに持たせる。
//   +1 に近い（良い）：住みやすい住宅、好影響を与える公園など
//   -1 に近い（悪い）：孤立した住宅、公害源の工業など
//
// 「悪い色のセル」を指して、周囲の建物から理由を説明できるようにするのが目的。
// ══════════════════════════════════════
void CellAutomaton::UpdateInfluence(int x, int z) {
	Cell& cell = grid_[x][z];

	switch (cell.type) {
	case CellType::RESIDENTIAL:
		// 住宅：自分の満足度をそのまま -1〜+1 にマップ（50を中立0とする）
		cell.influence = (cell.satisfaction - 50.0f) / 50.0f;
		break;

	case CellType::PARK:
		// 公園：周囲に良い影響。近くに住宅が多いほど貢献度が高い。
		cell.influence = 0.5f + std::min(0.5f, CountNearbyType(x, z, CellType::RESIDENTIAL, 2) * 0.1f);
		break;

	case CellType::INDUSTRIAL:
		// 工業：周囲に悪い影響（公害）。近くに住宅が多いほど害が大きい。
		cell.influence = -0.5f - std::min(0.5f, CountNearbyType(x, z, CellType::RESIDENTIAL, 3) * 0.15f);
		break;

	case CellType::COMMERCIAL:
		// 商業：軽い好影響（便利さ）。
		cell.influence = 0.3f;
		break;

	case CellType::ROAD:
		// 道路：中立。
		cell.influence = 0.0f;
		break;

	default:
		cell.influence = 0.0f;
		break;
	}

	cell.influence = std::max(-1.0f, std::min(1.0f, cell.influence));
}

// ★ 寄与度を段階別テクスチャに変換
//    住宅のみ満足度で色分け（5段階）。空きマスは白、住宅以外の建物は黒。
//    住宅：悪い ←─────────────────→ 良い
//          [紫]    [赤]    [白]    [黄緑]   [緑]
//         <=-0.6  <-0.1  <=0.3   <=0.65   それ以上
uint32_t CellAutomaton::InfluenceToTexture(const Cell& cell) {
	// 空きマスは白
	if (cell.type == CellType::EMPTY)
		return heatEmpty_;

	// 住宅以外の建物（道路・商業・工業・公園）は黒
	if (cell.type != CellType::RESIDENTIAL)
		return heatOther_;

	float v = cell.influence;
	if (v <= -0.6f)
		return heatStrongBad_; // 強い悪影響
	else if (v < -0.1f)
		return heatBad_; // 悪影響
	else if (v <= 0.3f)
		return heatNeutral_; // 中立
	else if (v <= 0.65f)
		return heatGood_; // やや好影響
	else
		return heatVeryGood_; // 強い好影響
}

// 住宅の人口を更新
// ★ 満足度がそのまま「目標人口」を決める。
//    満足度0なら人口0、満足度100なら最大人口。
//    立地の良し悪し（道路・公園・商業・工業）は
//    すべて満足度に集約されているので、ここはシンプル。
void CellAutomaton::SimulateResidential(int x, int z) {
	Cell& cell = grid_[x][z];

	int maxPop = 100 * (cell.level + 1);
	int targetPop = static_cast<int>(maxPop * (cell.satisfaction / 100.0f));

	// 目標に向かって毎ターン±10ずつ近づく
	if (cell.population < targetPop)
		cell.population = std::min(targetPop, cell.population + 10);
	else if (cell.population > targetPop)
		cell.population = std::max(targetPop, cell.population - 10);

	cell.income = cell.population * 0.05f;
}

// 商業施設の収入を更新
// 道路接続＋近くの住宅数で利用客（=人口）が決まる
void CellAutomaton::SimulateCommercial(int x, int z) {
	Cell& cell = grid_[x][z];

	if (!IsAdjacentToRoad(x, z)) {
		cell.population = std::max(0, cell.population - 5);
		cell.income = cell.population * 0.15f;
		return;
	}

	int targetWorkers = 30 * (cell.level + 1);
	targetWorkers += CountNearbyType(x, z, CellType::RESIDENTIAL, 4) * 5; // 近くの住民が客
	targetWorkers = std::max(0, targetWorkers);

	if (cell.population < targetWorkers)
		cell.population = std::min(targetWorkers, cell.population + 5);
	else if (cell.population > targetWorkers)
		cell.population = std::max(targetWorkers, cell.population - 5);

	cell.income = cell.population * 0.15f;
}

// 工業施設の収入を更新
// 道路接続で操業できるが、住宅が近いと効率が下がる
void CellAutomaton::SimulateIndustrial(int x, int z) {
	Cell& cell = grid_[x][z];

	if (!IsAdjacentToRoad(x, z)) {
		cell.population = std::max(0, cell.population - 5);
		cell.income = cell.population * 0.10f;
		return;
	}

	int targetWorkers = 40 * (cell.level + 1);
	targetWorkers -= CountNearbyType(x, z, CellType::RESIDENTIAL, 3) * 8; // 住宅が近いと操業効率↓
	targetWorkers = std::max(0, targetWorkers);

	if (cell.population < targetWorkers)
		cell.population = std::min(targetWorkers, cell.population + 5);
	else if (cell.population > targetWorkers)
		cell.population = std::max(targetWorkers, cell.population - 5);

	cell.income = cell.population * 0.10f;
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

// 全セルのシミュレーションを1ステップ実行
void CellAutomaton::RunSimulation() {
	// ★ まず現在の満足度を前ターン値としてスナップショット
	//    （このあとの伝播計算は、すべてこの固定値を読む＝走査順に依存しない）
	for (int x = 0; x < GRID_SIZE; ++x)
		for (int z = 0; z < GRID_SIZE; ++z)
			prevSatisfaction_[x][z] = grid_[x][z].satisfaction;

	// 満足度を更新（人口計算の入力になる）
	for (int x = 0; x < GRID_SIZE; ++x)
		for (int z = 0; z < GRID_SIZE; ++z)
			UpdateSatisfaction(x, z);

	// 満足度に基づいて人口・収入を更新
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

	// ★ 満足度が確定した後に、町への寄与度（色分け用）を更新
	for (int x = 0; x < GRID_SIZE; ++x)
		for (int z = 0; z < GRID_SIZE; ++z)
			UpdateInfluence(x, z);
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
	grid_[x][z].satisfaction = 50.0f; // ★ 配置時に満足度を初期化
	grid_[x][z].influence = 0.0f;     // ★ 寄与度も初期化
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

// ★ 通常描画：3Dの建物
void CellAutomaton::DrawNormal() {
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
}

// ★ ヒートマップ描画：建物を隠し、床に平たいタイルを敷いて寄与度の色で塗る
//    真上から見ると、街全体の「良し悪しの地図」になる。
void CellAutomaton::DrawHeatmap() {
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			Cell& cell = grid_[x][z];
			KamataEngine::WorldTransform* t = cell.worldTransform_;
			t->translation_.x = static_cast<float>(x) + 0.5f;
			t->translation_.z = static_cast<float>(z) + 0.5f;
			t->translation_.y = 0.02f;       // 床すれすれ
			t->scale_ = {0.5f, 0.02f, 0.5f}; // 平たいタイル（建物は描かない）

			WorldTransformUpdate(*t);
			cellModel_->Draw(*t, *camera_, InfluenceToTexture(cell));
		}
	}
}

void CellAutomaton::Draw(KamataEngine::PrimitiveDrawer* drawer) {
	if (!cellModel_ || !camera_)
		return;

	// ★ 表示モードで描画を切り替え
	if (displayMode_ == DisplayMode::Heatmap) {
		DrawHeatmap();
	} else {
		DrawNormal();
	}

	// カーソル（どちらのモードでも表示）
	cursorWorldTransform_.translation_.x = static_cast<float>(cursorX_) + 0.5f;
	cursorWorldTransform_.translation_.z = static_cast<float>(cursorZ_) + 0.5f;
	cursorWorldTransform_.translation_.y = 0.04f;
	cursorWorldTransform_.scale_ = {0.5f, 0.04f, 0.5f};
	WorldTransformUpdate(cursorWorldTransform_);
	cellModel_->Draw(cursorWorldTransform_, *camera_, cursorTexture_);

	DrawCursor(drawer);
}