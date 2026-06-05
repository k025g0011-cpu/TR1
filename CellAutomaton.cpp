#define NOMINMAX
#include "CellAutomaton.h"
#include "WorldTransform.h"
#include <algorithm>

using namespace KamataEngine;

/// <summary>
/// コンストラクタ。30x30マスのメモリ確保と、各セルの3D位置行列（WorldTransform）の初期化を行う。
/// </summary>
CellAutomaton::CellAutomaton() {
	// グリッドを30行30列でリサイズ
	// 第二引数でCell構造体の入った横列を30個作り、それを第一引数の分だけ積み重ねるイメージ(30x30の二次元配列になる)
	grid_.resize(GRID_SIZE, std::vector<Cell>(GRID_SIZE)); 


	// 全セルに対して、3D座標を管理するためのトランスフォーム構造体を生成して初期化
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			grid_[x][z].worldTransform_ = new WorldTransform();
			grid_[x][z].worldTransform_->Initialize();
		}
	}

	// 満足度の伝播計算で参照する「前ターンの満足度スナップショット用バッファ」を50.0（標準値）で初期確保
	prevSatisfaction_.resize(GRID_SIZE, std::vector<float>(GRID_SIZE, 50.0f));
}

/// <summary>
/// デストラクタ。各セルが個別に動的確保していたWorldTransformインスタンスを全て解放する。
/// </summary>
CellAutomaton::~CellAutomaton() {
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			delete grid_[x][z].worldTransform_;
		}
	}
}

/// <summary>
/// 各種アセット（通常テクスチャ、およびヒートマップ表示用の色テクスチャ）のロードを行う。
/// </summary>
void CellAutomaton::Initialize(Model* model, Camera* camera) {
	cellModel_ = model;
	camera_ = camera;

	// 通常グラフィック用テクスチャの読み込み
	groundTexture_ = TextureManager::Load("ground.png");
	cursorTexture_ = TextureManager::Load("white1x1.png");

	// 各建物の色テクスチャハンドルを登録
	textureHandles_[CellType::ROAD] = TextureManager::Load("gray1x1.png");
	textureHandles_[CellType::RESIDENTIAL] = TextureManager::Load("gide1x1.png"); // 住宅用
	textureHandles_[CellType::COMMERCIAL] = TextureManager::Load("blue1x1.png");
	textureHandles_[CellType::INDUSTRIAL] = TextureManager::Load("black1x1.png");
	textureHandles_[CellType::PARK] = TextureManager::Load("green1x1.png");

	// ヒートマップ用の各種カラーテクスチャの読み込み（1x1の単色カラーPNGファイルを流用）
	heatStrongBad_ = TextureManager::Load("darkRed1x1.png");  // 紫：最悪（※ファイル名はdarkRedだが仕様上紫の役割）
	heatBad_ = TextureManager::Load("red1x1.png");            // 赤：悪い
	heatNeutral_ = TextureManager::Load("white1x1.png");      // 白：中立
	heatGood_ = TextureManager::Load("green1x1.png");         // 黄緑：良い
	heatVeryGood_ = TextureManager::Load("darkGreen1x1.png"); // 緑：最高
	heatOther_ = TextureManager::Load("black1x1.png");        // 黒：住宅以外
	heatEmpty_ = TextureManager::Load("white1x1.png");        // 白：さら地

	// 3D空間上のカーソル表示用トランスフォームの初期化
	cursorWorldTransform_.Initialize();
}

// ユーザー入力・ゲーム画面との連携インターフェース
void CellAutomaton::MoveCursor(int dx, int dz) {
	// 移動先が30x30の境界線を超えないように0〜29の間でクランプ（制限）する
	cursorX_ = std::max(0, std::min(GRID_SIZE - 1, cursorX_ + dx));
	cursorZ_ = std::max(0, std::min(GRID_SIZE - 1, cursorZ_ + dz));
}

// =================================================================
// 住民・経営シミュレーションのロジックコア部分
// =================================================================

/// <summary>
/// 道路アクセス判定。対象マスの上下左右（4方向）に道路セルが隣接しているかを調査する。
/// </summary>
bool CellAutomaton::IsAdjacentToRoad(int x, int z) {
	const int dx[] = {0, 0, 1, -1};
	const int dz[] = {1, -1, 0, 0};

	for (int i = 0; i < 4; ++i) {
		int nx = x + dx[i], nz = z + dz[i];
		// グリッド範囲外のインデックスを指した場合は無視してスキップ（配列外参照バグ防止）
		if (nx < 0 || nx >= GRID_SIZE || nz < 0 || nz >= GRID_SIZE)
			continue;
		if (grid_[nx][nz].type == CellType::ROAD)
			return true; // 1つでも道路が見つかれば即座に接続完了とする
	}
	return false; // 4方向すべて道路でなければ未接続
}

/// <summary>
/// 周辺環境の走査。自分の中心座標から一定マス（半径radius）の正方形の範囲に、指定した種類の建物が何個存在するかをカウントする。
/// </summary>
int CellAutomaton::CountNearbyType(int x, int z, CellType type, int radius) {
	int count = 0;
	for (int dx = -radius; dx <= radius; ++dx) {
		for (int dz = -radius; dz <= radius; ++dz) {
			if (dx == 0 && dz == 0)
				continue; // 自分自身のマスはカウントから除外

			int nx = x + dx, nz = z + dz;
			if (nx < 0 || nx >= GRID_SIZE || nz < 0 || nz >= GRID_SIZE)
				continue; // 街の外枠をはみ出した座標は無視

			if (grid_[nx][nz].type == type)
				count++;
		}
	}
	return count;
}

/// <summary>
/// 老朽化に伴う補正倍率の算出。建設後のターン数（age）を元に、建物の稼働効率(0.20〜1.00)を弾き出す。
/// </summary>
float CellAutomaton::AgeEfficiency(int age) const {
	if (age <= AGE_GRACE)
		return 1.0f; // 20ターン以内であれば100%フル稼働

	// 20ターンを越えた場合、1ターンごとに効率が0.02(2%)ずつ線形減少する
	float eff = 1.0f - (age - AGE_GRACE) * AGE_DECAY;

	// 最低でも20%の効率（AGE_MIN_EFF）は下回らないように底打ち処理を行う
	return std::max(AGE_MIN_EFF, eff);
}

/// <summary>
/// 満足度の近隣伝播処理用サブルーチン。半径1マスの範囲にある「他の住宅」の、前ターン時点での満足度平均値を算出する。
/// </summary>
float CellAutomaton::AverageNeighborSatisfaction(int x, int z, int radius) {
	float total = 0.0f;
	int count = 0;
	for (int dx = -radius; dx <= radius; ++dx) {
		for (int dz = -radius; dz <= radius; ++dz) {
			if (dx == 0 && dz == 0)
				continue;

			int nx = x + dx, nz = z + dz;
			if (nx < 0 || nx >= GRID_SIZE || nz < 0 || nz >= GRID_SIZE)
				continue;

			if (grid_[nx][nz].type == CellType::RESIDENTIAL) {
				total += prevSatisfaction_[nx][nz]; // リアルタイムの書き換えバグを防ぐため、前ターンの確定バッファを参照
				count++;
			}
		}
	}


	if (count > 0) {
		// 1軒以上あったら、平均値を計算して返す
		return total / count;
	} else {
		// 0軒（ぼっち住宅）なら、特殊フラグとして -1.0f を返す
		return -1.0f;
	}
}

/// <summary>
/// 【重要論理】住宅セルの住民満足度を2段階ステップ（立地条件＋近隣伝播）で決定する。
/// </summary>
void CellAutomaton::UpdateSatisfaction(int x, int z) {
	Cell& cell = grid_[x][z];

	if (cell.type != CellType::RESIDENTIAL)
		return; // 住宅以外の建物には満足度の概念がないため処理スルー

	// ── 1段目：純粋な周囲の「立地条件」からベースとなるスコア(s)を決定 ──
	float s = 50.0f; // フラットな初期評価値

	// インフラ接続：道路と繋がっていれば+20点、孤立していればスラム化ペナルティとして-50点（致命傷）
	s += IsAdjacentToRoad(x, z) ? 20.0f : -50.0f;

	// 環境スコアの集計（半径2マスの公園は1個につき+15点、半径3マスの商業施設は+15点、工業公害は1個につき-30点の強烈な減点）
	s += CountNearbyType(x, z, CellType::PARK, 2) * 15.0f;
	s += CountNearbyType(x, z, CellType::COMMERCIAL, 3) * 15.0f;
	s -= CountNearbyType(x, z, CellType::INDUSTRIAL, 3) * 30.0f;

	// ── 2段目：近所づきあい（満足度の周囲伝播エフェクト） ──
	// 周りの住宅街が裕福・快適であればつられて治安や価値が上昇し、周囲が荒れていると自分も引っ張られる現象を模倣
	const float spreadFactor = 0.3f;                          // 伝播の吸い寄せ強度（30%）
	float neighborAvg = AverageNeighborSatisfaction(x, z, 1); // 隣接住宅の平均スコア

	if (neighborAvg >= 0.0f) {
		// 近所に他の住宅がある場合のみ、立地ベース値(s)と近所平均の差分の30%分だけ自分の満足度を引き寄せる
		s += (neighborAvg - s) * spreadFactor;
	}

	// 計算結果がマイナスになったり100をオーバーしたりしないように0.0〜100.0の間へクランプして保存
	cell.satisfaction = std::max(0.0f, std::min(100.0f, s));
}

/// <summary>
/// 【重要論理】住宅セルの人口動態の決定。上記で求めた「住民満足度」に基づいて人口が自律増減する。
/// </summary>
void CellAutomaton::SimulateResidential(int x, int z) {
	Cell& cell = grid_[x][z];

	int maxPop = 100 * (cell.level + 1); // 1マスあたりの最大収容人口の上限（現在はレベル0なので上限100人）
	float sat = cell.satisfaction;

	// 満足度の段階に応じて、目標値への収束ではなく、毎ターン連続して増減を起こす動的変動システム
	if (sat >= 70.f) {
		// 満足度70以上の超快適な街：毎ターン1〜4人ずつ人口が流入・引っ越してくる
		int gain = static_cast<int>((sat - 70.f) / 10.f) + 1;
		cell.population = std::min(maxPop, cell.population + gain); // 上限100人でストップ
	} else if (sat >= 40.f) {
		// 満足度40〜70の普通の街：人口は現状維持で安定
	} else if (sat >= 20.f) {
		// 満足度20〜40のやや不快な街：不満により毎ターン1〜3人ずつ住民が家を出ていく（人口減少）
		int loss = static_cast<int>((40.f - sat) / 10.f) + 1;
		cell.population = std::max(0, cell.population - loss);
	} else {
		// 満足度20以下の劣悪な街（インフラ寸断や公害直撃）：毎ターン3〜8人ペースで住民が逃げ出す（過疎化・ゴーストタウン化）
		int loss = static_cast<int>((20.f - sat) / 4.f) + 3;
		cell.population = std::max(0, cell.population - loss);
	}

	// 住宅自体は直接税金を納めない仕様（人口を集めることに特化）
	cell.income = 0.0f;
}

/// <summary>
/// 【重要論理】商業セルの経営シミュレーション。近隣の住宅人口を吸い上げて売上にする。
/// </summary>
void CellAutomaton::SimulateCommercial(int x, int z) {
	Cell& cell = grid_[x][z];

	// 商業地が道路に面していない場合、仕入れも客の来店もできないため、稼働度および売上は完全ゼロになる
	if (!IsAdjacentToRoad(x, z)) {
		cell.population = 0;
		cell.income = 0.0f;
		return;
	}

	// 周辺半径4マス以内に居住している「住宅セルの住民人口の合計」を計算し、潜在顧客数（customers）とする
	int customers = 0;
	const int radius = 4;
	for (int dx = -radius; dx <= radius; ++dx) {
		for (int dz = -radius; dz <= radius; ++dz) {
			if (dx == 0 && dz == 0)
				continue;
			int nx = x + dx, nz = z + dz;
			if (nx < 0 || nx >= GRID_SIZE || nz < 0 || nz >= GRID_SIZE)
				continue;
			if (grid_[nx][nz].type == CellType::RESIDENTIAL)
				customers += grid_[nx][nz].population;
		}
	}

	// お店が一度に捌くことができる「基本キャパシティ（店員の限界数）」を設定（レベル0なら上限200人）
	float capacity = 200.0f * (cell.level + 1);

	// 産業シナジー：半径4マスの中に工業（工場）があると、商品の仕入れ能力が上がり店が活性化すると見なす
	// 工場1つあたりキャパシティ上限が+50%され、最大+150%（4倍近くの客を捌けるメガストア化）まで拡張される
	int factories = CountNearbyType(x, z, CellType::INDUSTRIAL, radius);
	float boost = 1.0f + std::min(1.5f, factories * 0.5f);
	capacity *= boost;

	// 潜在顧客がどれだけ多くても、お店の限界キャパシティで頭打ち（制限）される
	int served = std::min(customers, static_cast<int>(capacity));

	// 実際に接客した客の数を、セルの稼働パラメータ（population）に保存してUIから覗けるようにする
	cell.population = served;

	// 商業収入の確定：捌いた客1人あたり$0.15の利益。ここに「経年老朽化による効率倍率」を乗算して最終売上とする
	cell.income = served * 0.15f * AgeEfficiency(cell.age);
}

/// <summary>
/// 【重要論理】工業セルの経営シミュレーション。住民に関係なく安定稼ぐが、周囲を汚染する。
/// </summary>
void CellAutomaton::SimulateIndustrial(int x, int z) {
	Cell& cell = grid_[x][z];

	// 道路がないと製品を都市外へ出荷できないため操業不能（売上ゼロ）
	if (!IsAdjacentToRoad(x, z)) {
		cell.population = 0;
		cell.income = 0.0f;
		return;
	}

	// 工場は労働者人口の有無を問わず、インフラさえあれば一定の生産力を発揮する（レベル0なら固定出力30）
	int baseOutput = 30 * (cell.level + 1);
	cell.population = baseOutput; // 表示用に操業出力を記録

	// 工業収入の確定：生産量1あたり$0.4の安定した高利益を叩き出す。ただし商業同様に老朽化効率の影響を受ける
	cell.income = baseOutput * 0.4f * AgeEfficiency(cell.age);
}

/// <summary>
/// 都市全体の平均満足度の算出（ImGuiダッシュボードのメインメーター用）。
/// </summary>
float CellAutomaton::GetAverageSatisfaction() const {
	float total = 0.0f;
	int count = 0;
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			if (grid_[x][z].type == CellType::RESIDENTIAL) {
				total += grid_[x][z].satisfaction; // 住宅のみの満足度を加算
				count++;
			}
		}
	}
	return count > 0 ? total / count : 0.0f; // 都市に住宅が1つもない場合は0%とする
}

/// <summary>
/// ヒートマップ可視化用の寄与度パラメーターの計算。全セルの状態を-1.0(最悪)〜+1.0(最高)の範囲に落とし込む。
/// </summary>
void CellAutomaton::UpdateInfluence(int x, int z) {
	Cell& cell = grid_[x][z];

	switch (cell.type) {
	case CellType::RESIDENTIAL:
		// 住宅：自身の住民満足度(0〜100)を、50を中立(0.0)とする-1.0〜+1.0の範囲に線形マップ
		cell.influence = (cell.satisfaction - 50.0f) / 50.0f;
		break;

	case CellType::PARK:
	case CellType::INDUSTRIAL:
	case CellType::COMMERCIAL:
	case CellType::ROAD:
	default:
		cell.influence = 0.0f;
		break;
	}

	// 念のため-1.0〜+1.0の範囲をはみ出さないように安全ガード
	cell.influence = std::max(-1.0f, std::min(1.0f, cell.influence));
}

/// <summary>
/// 計算されたInfluence(影響度)の値を、5段階の視覚的なヒートマップ用単色テクスチャに紐付ける。
/// </summary>
uint32_t CellAutomaton::InfluenceToTexture(const Cell& cell) {
	if (cell.type == CellType::EMPTY)
		return heatEmpty_; // さら地は一律で「白」
	if (cell.type != CellType::RESIDENTIAL)
		return heatOther_; // 住宅以外のすべての建物は、地図の骨格として「黒」でマスク

	// ── 以下、住宅セルの満足度に応じたカラーマップの割り当て ──
	float v = cell.influence;
	if (v <= -0.6f)
		return heatStrongBad_; // 紫：スラム街・最悪環境
	else if (v < -0.1f)
		return heatBad_; // 赤：道路が途絶えたか公害が隣接して不満
	else if (v <= 0.3f)
		return heatNeutral_; // 白：可もなく不可もない普通の家
	else if (v <= 0.65f)
		return heatGood_; // 黄緑：公園やお店が近くにあって快適
	else
		return heatVeryGood_; // 緑：最高のロケーションを誇る高級住宅街
}

/// <summary>
/// 【最重要シミュレーション進行ループ】シミュレーションタイマーが満期を迎えた瞬間に、都市全体の時間を1ステップ進める。
/// </summary>
void CellAutomaton::RunSimulation() {
	// 【前処理】現在の満足度を伝播計算用の参照バッファ(prevSatisfaction_)へ一斉に全コピー（スナップショット撮影）
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			prevSatisfaction_[x][z] = grid_[x][z].satisfaction;
		}
	}

	// 1. さら地以外のすべての建物の築年数（age）を+1ターン増やす
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			if (grid_[x][z].type != CellType::EMPTY) {
				grid_[x][z].age++;
			}
		}
	}

	// 2. 全マスの新しい「満足度」を確定（前ターンの周囲データを読むため、計算の走査順に依存しない）
	for (int x = 0; x < GRID_SIZE; ++x){
		for (int z = 0; z < GRID_SIZE; ++z) {
			UpdateSatisfaction(x, z);
		}
	}

	// 3. 確定した新しい満足度を基準にして、各建物の「人口増減」「商業売上」「工業生産」を順次計算
	for (int x = 0; x < GRID_SIZE; ++x) {
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

	// 4. 計算が終わった最終ステータスから、ヒートマップ用の街への影響度を再計算
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			UpdateInfluence(x, z);
		}
	}

	// 5. 【財政集計】都市全域の経済活動の売上と、建物の維持費を集計する
	float income = 0.0f; // 全体の売上総額を蓄積する変数

	float comIncome = 0.0f, indIncome = 0.0f; // 商業と工業の売上を個別に集計する変数
	float maintenance = 0.0f;                 // 建物の維持費総額を蓄積する変数

	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			Cell& c = grid_[x][z];
			income += c.income; // 個別セルの売上を全体売上に加算

			if (c.type == CellType::COMMERCIAL)
				comIncome += c.income;
			else if (c.type == CellType::INDUSTRIAL)
				indIncome += c.income;

			// 建物固有の維持費をインライン構造体から取得して合算
			maintenance += GetBuildingCost(c.type).maintCost;
		}
	}

	// 今回のターンの国家の「純利益（売上総額 - 維持費総額）」を算出。赤字ならマイナス値になる
	float net = income - maintenance;

	// UI表示ウィンドウ用に数値を一時退避して記憶
	lastTurnIncome_ = income;
	lastTurnMaintenance_ = maintenance;
	lastTurnNet_ = net;

	// GameSceneクラスがCollectIncome()を呼び出して国庫に回収しに来るまで、未回収バッファに利益をプール
	pendingIncome_ += net;

	// 6. 【ログ記録】ImGuiの推移グラフウィンドウに表示するため、末尾に最新データをプッシュ
	IncomeLogEntry incEntry;
	incEntry.total = income;
	incEntry.commercial = comIncome;
	incEntry.industrial = indIncome;
	incEntry.maintenance = maintenance;
	incEntry.net = net;
	incomeLog_.push_back(incEntry);

	if ((int)incomeLog_.size() > MAX_LOG)
		incomeLog_.erase(incomeLog_.begin()); // 20件を越えた古いログはメモリ節約のため先頭から削除

	// 人口動態ログの記録
	int currentPop = GetTotalPopulation();
	PopLogEntry entry;
	entry.total = currentPop;
	entry.delta = currentPop - prevTotalPop_; // 今回の人口 - 前回の人口 = 増減数
	prevTotalPop_ = currentPop;               // 次回計算用に保存
	popLog_.push_back(entry);

	if ((int)popLog_.size() > MAX_LOG)
		popLog_.erase(popLog_.begin());
}

/// <summary>
/// 都市全域の「住宅セル」の人口のみを足し合わせて、総人口を返す。
/// </summary>
int CellAutomaton::GetTotalPopulation() const {
	int total = 0;
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			if (grid_[x][z].type == CellType::RESIDENTIAL) {
				total += grid_[x][z].population;
			}
		}
	}
	return total;
}

/// <summary>
/// 毎フレームのUpdate。シミュレーション有効時のみタイマーを秒単位で進め、指定インターバルに達したらRunSimulationを実行する。
/// </summary>
void CellAutomaton::Update(float deltaTime) {
	if (!enableSimulation_)
		return; // チェックボックスでシミュレーションが停止していれば何もしない

	simTimer_ += deltaTime; // 60FPSなら約0.0166秒ずつ加算
	if (simTimer_ >= simInterval_) {
		simTimer_ = 0.0f; // タイマーをリセット
		RunSimulation();  // 1ターン時計を進める
	}
}

/// <summary>
/// プレイヤーが建物を新しく建てた、または壊した際のデータグリッド書き換え処理。
/// </summary>
void CellAutomaton::PlaceCell(int x, int z, CellType type) {
	if (x < 0 || x >= GRID_SIZE || z < 0 || z >= GRID_SIZE) {
		return; // グリッド外の誤作動ガード
	}


	// 新しい建物種別の上書きと、付随する個別ステータスの完全初期化
	grid_[x][z].type = type;
	grid_[x][z].level = 0;
	grid_[x][z].population = 0;
	grid_[x][z].income = 0.0f;
	grid_[x][z].satisfaction = 50.0f; // 新築時の標準満足度は50%
	grid_[x][z].influence = 0.0f;
	grid_[x][z].age = 0; // 建て替えにより築年数が0に若返り、老朽化による収入減退ペナルティが完全リセットされる
}

void CellAutomaton::PlaceCellAtCursor(CellType type) { PlaceCell(cursorX_, cursorZ_, type); }
void CellAutomaton::RemoveCell(int x, int z) { PlaceCell(x, z, CellType::EMPTY); }

Cell* CellAutomaton::GetCell(int x, int z) {
	if (x < 0 || x >= GRID_SIZE || z < 0 || z >= GRID_SIZE)
		return nullptr;
	return &grid_[x][z];
}

// =================================================================
// グラフィックス・描画制御部分
// =================================================================

/// <summary>
/// 選択中のマスを囲う白い枠線と、建物が存在する場合に近隣8マスを黄色く強調するデバッグ用グリッドアート。
/// </summary>
void CellAutomaton::DrawCursor(PrimitiveDrawer* drawer) {
	if (!drawer)
		return;
	const float y = 0.1f; // 地面の線(Y=0.05)と被らないようにわずかに上に浮かせて描画

	// カーソルがあるマス(1x1)を白線で囲う
	float cx0 = static_cast<float>(cursorX_), cx1 = cx0 + 1.0f;
	float cz0 = static_cast<float>(cursorZ_), cz1 = cz0 + 1.0f;
	KamataEngine::Vector4 white = {1, 1, 1, 1};
	drawer->DrawLine3d({cx0, y, cz0}, {cx1, y, cz0}, white);
	drawer->DrawLine3d({cx1, y, cz0}, {cx1, y, cz1}, white);
	drawer->DrawLine3d({cx1, y, cz1}, {cx0, y, cz1}, white);
	drawer->DrawLine3d({cx0, y, cz1}, {cx0, y, cz0}, white);

	// カーソル下に何かしらの建物が建っている場合、影響範囲の視覚化として「周囲8マス」を黄色い枠線で包み込む
	Cell* c = GetCell(cursorX_, cursorZ_);
	if (!c || c->type == CellType::EMPTY)
		return;

	Vector4 yellow = {1, 1, 0, 1};
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

/// <summary>
/// 通常モードの3D都市レンダリング。建物の種類に基づいてモデルの「高さ（Y軸スケール）」や幅を変更し、ビル群のシルエットを作る。
/// </summary>
void CellAutomaton::DrawNormal() {
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			Cell& cell = grid_[x][z];
			WorldTransform* t = cell.worldTransform_;

			// マスのインデックス(0〜29)の中心位置(0.5ずらす)を3DのX,Z座標へマッピング
			t->translation_.x = static_cast<float>(x) + 0.5f;
			t->translation_.z = static_cast<float>(z) + 0.5f;

			// さら地の場合は、地面にぴったり吸い付いた平たい緑の絨毯タイルとして描く
			if (cell.type == CellType::EMPTY) {
				t->translation_.y = 0.0f;
				t->scale_ = {0.5f, 0.02f, 0.5f};
				WorldTransformUpdate(*t); // 行列計算を反映
				cellModel_->Draw(*t, *camera_, groundTexture_);
				continue;
			}

			// 建物ごとに高さを変えて都市の凹凸を表現する
			float height = 0.5f, scaleXZ = 0.45f;
			switch (cell.type) {
			case CellType::ROAD:
				height = 0.05f; // 道路は床にへばりつく薄いアスファルト板
				scaleXZ = 0.5f; // マス目いっぱいに広げる
				break;
			case CellType::RESIDENTIAL:
				height = 0.6f + cell.level * 0.5f; // 普通の高さの家
				scaleXZ = 0.44f;
				break;
			case CellType::COMMERCIAL:
				height = 0.9f + cell.level * 0.7f; // 商業ビルは少し高層化する
				scaleXZ = 0.40f;                   // 少しスタイリッシュに細身にする
				break;
			case CellType::INDUSTRIAL:
				height = 0.7f + cell.level * 0.5f; // 重厚感のある工場
				scaleXZ = 0.44f;
				break;
			case CellType::PARK:
				height = 0.1f; // 公園は緑地の少し高くなった広場
				scaleXZ = 0.48f;
				break;
			default:
				break;
			}

			t->translation_.y = height;             // 立方体モデルの中心点のY座標を高さに合わせる
			t->scale_ = {scaleXZ, height, scaleXZ}; // スケールを適用して縦長のボックスに変形
			WorldTransformUpdate(*t);

			// 登録された建物の単色カラーテクスチャを貼り付けてポリゴンを描画
			cellModel_->Draw(*t, *camera_, textureHandles_[cell.type]);
		}
	}
}

/// <summary>
/// ヒートマップモードの2Dカラーグリッドレンダリング。すべての建物の高さを潰し、地面すれすれに寄与度の色タイルを敷き詰める。
/// </summary>
void CellAutomaton::DrawHeatmap() {
	for (int x = 0; x < GRID_SIZE; ++x) {
		for (int z = 0; z < GRID_SIZE; ++z) {
			Cell& cell = grid_[x][z];
			KamataEngine::WorldTransform* t = cell.worldTransform_;

			t->translation_.x = static_cast<float>(x) + 0.5f;
			t->translation_.z = static_cast<float>(z) + 0.5f;
			t->translation_.y = 0.02f;       // 地面すれすれの位置に固定
			t->scale_ = {0.5f, 0.02f, 0.5f}; // 全て等しく完全に平坦な正方形タイル化（高さを潰す）

			WorldTransformUpdate(*t);
			// 影響度に応じたテクスチャ色(InfluenceToTexture)を選択して床を塗りつぶす
			cellModel_->Draw(*t, *camera_, InfluenceToTexture(cell));
		}
	}
}

/// <summary>
/// 外部から呼ばれるメイン描画エントリ。モードに応じた描画ルーチンを呼び分け、最後にプレイヤー用の黄色いカーソルを重畳する。
/// </summary>
void CellAutomaton::Draw(KamataEngine::PrimitiveDrawer* drawer) {
	if (!cellModel_ || !camera_)
		return;

	// モード選択フラグによる関数のスイッチング
	if (displayMode_ == DisplayMode::Heatmap) {
		DrawHeatmap(); // ヒートマップ地図を描画
	} else {
		DrawNormal(); // 通常の3Dビル街を描画
	}

	// ── プレイヤーが操作するフォーカスカーソルの立体描画（半透明の白い浮遊キューブ） ──
	cursorWorldTransform_.translation_.x = static_cast<float>(cursorX_) + 0.5f;
	cursorWorldTransform_.translation_.z = static_cast<float>(cursorZ_) + 0.5f;
	cursorWorldTransform_.translation_.y = 0.04f;
	cursorWorldTransform_.scale_ = {0.5f, 0.04f, 0.5f};
	WorldTransformUpdate(cursorWorldTransform_);
	cellModel_->Draw(cursorWorldTransform_, *camera_, cursorTexture_);

	// カーソルの四角い白枠・黄色枠線群をPrimitiveDrawerを使って重ね書き
	DrawCursor(drawer);
}

/// <summary>
/// 会計処理：GameSceneに未回収資金を引き渡し、二重受け取りを完全に防ぐためにプールをゼロに戻す。
/// </summary>
float CellAutomaton::CollectIncome() {
	float v = pendingIncome_;
	pendingIncome_ = 0.0f; // バッファのクリア
	return v;              // 溜まっていた純収益を返却
}