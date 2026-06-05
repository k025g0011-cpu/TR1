#pragma once
#include "KamataEngine.h"
#include <unordered_map>
#include <vector>

// 都市を構成する建物の種類を定義する列挙型
enum class CellType {
	EMPTY,       // 0: 空き地（さら地）
	ROAD,        // 1: 道路（インフラ・これがないと建物が機能しない）
	RESIDENTIAL, // 2: 住宅（住民が住む場所。収入は生まないが人口を増やす）
	COMMERCIAL,  // 3: 商業（お店。近くの住民を集客して利益を出す。維持費あり）
	INDUSTRIAL,  // 4: 工業（工場。道路さえあれば安定して稼ぐが、公害の元。維持費あり）
	PARK         // 5: 公園（環境保全。周囲の住宅の満足度を激変させる）
};

// 通常の3D建築表示か、寄与度を表す2Dカラータイル表示かを定義する列挙型
enum class DisplayMode { Normal, Heatmap };

/// <summary>
/// 最小単位となる「1マス（セル）」の内部データ構造体。
/// </summary>
struct Cell {
	CellType type = CellType::EMPTY;                         // セルに配置されている建物の種類
	int level = 0;                                           // 拡張用：建物のレベル（現在は0固定）
	int population = 0;                                      // 人口、または産業セルの「現在稼働度（客数・出荷量）」
	float income = 0.0f;                                     // このセルが直近の1ターンに稼ぎ出した個別収入
	float satisfaction = 50.0f;                              // 住宅セル専用：住民満足度 (0.0〜100.0)
	float influence = 0.0f;                                  // ヒートマップ色分け用の街への寄与度 (-1.0〜+1.0)
	int age = 0;                                             // 築年数（建設されてから経過したシミュレーションターン数）
	KamataEngine::WorldTransform* worldTransform_ = nullptr; // 3D空間上での位置・回転・縮尺行列の管理ポインタ
};

/// <summary>
/// 建物の財政的パラメータ（建設コストとターン毎の維持費）を定義する構造体。
/// </summary>
struct BuildingCost {
	float buildCost; // 初期建設費用（プレイヤーの資金から引かれる）
	float maintCost; // 1ターン毎にかかる固定維持費（プレイヤーの資金から自動引き落とし）
};

/// <summary>
/// セルの種類に応じた費用パラメータを即座に引き出すインライン関数。
/// バランス調整時はここの数値を変更する。
/// </summary>
inline BuildingCost GetBuildingCost(CellType type) {	//inlineは処理を呼び出し元に展開して高速化。
	switch (type) {
	case CellType::ROAD:
		return {100.0f, 0.0f}; // 道路: 建設$100、維持費なし
	case CellType::RESIDENTIAL:
		return {300.0f, 0.0f}; // 住宅: 建設$300、維持費なし
	case CellType::COMMERCIAL:
		return {500.0f, 20.0f}; // 商業: 建設$500、維持費$20（店舗維持）
	case CellType::INDUSTRIAL:
		return {400.0f, 15.0f}; // 工業: 建設$400、維持費$15（操業コスト）
	case CellType::PARK:
		return {200.0f, 0.0f}; // 公園: 建設$200、維持費なし
	default:
		return {0.0f, 0.0f};
	}
}

// 人口ログの1レコード構造体
struct PopLogEntry {
	int total; // そのターンの都市の全人口合計
	int delta; // 前ターンからの人口増減（差分）
};

// 財政収支ログの1レコード構造体
struct IncomeLogEntry {
	float total;       // 総収入（維持費を引く前の純粋な稼ぎの合計）
	float commercial;  // 商業セルの収入合計
	float industrial;  // 工業セルの収入合計
	float maintenance; // 都市全体の建物維持費の総支出額
	float net;         // 純収益（プレイヤーの手元に残る、または減る額 = total - maintenance）
};

/// <summary>
/// 都市シミュレーションのグリッド管理と自動変化（オートマトン）の主処理を担うクラス。
/// </summary>
class CellAutomaton {
private:
	static const int GRID_SIZE = 30;      // 都市のサイズ定義（30x30の固定正方形グリッド）
	std::vector<std::vector<Cell>> grid_; // 30x30の二次元配列で表される都市の全セルデータ

	KamataEngine::Model* cellModel_ = nullptr; // 共通で使い回す立方体3Dモデル
	KamataEngine::Camera* camera_ = nullptr;   // 描画時に必要なゲームシーンのカメラへの参照

	// ── 通常描画用テクスチャハンドル ──
	uint32_t groundTexture_ = 0;                            // さら地用の草地テクスチャ
	uint32_t cursorTexture_ = 0;                            // 選択カーソルの土台となる白い板テクスチャ
	std::unordered_map<CellType, uint32_t> textureHandles_; // 建物タイプごとの単色カラーテクスチャの連想配列

	// ── ヒートマップ描画用テクスチャハンドル ──
	uint32_t heatStrongBad_ = 0; // 紫：最悪の影響
	uint32_t heatBad_ = 0;       // 赤：悪影響
	uint32_t heatNeutral_ = 0;   // 白：標準・中立
	uint32_t heatGood_ = 0;      // 黄緑：良好な影響
	uint32_t heatVeryGood_ = 0;  // 緑：最高の状態
	uint32_t heatOther_ = 0;     // 黒：住宅以外のインフラ・産業セル
	uint32_t heatEmpty_ = 0;     // 白：さら地

	// ── カーソル管理 ──
	KamataEngine::WorldTransform cursorWorldTransform_; // 3D空間上でカーソルの床板を置くための座標データ
	int cursorX_ = 15, cursorZ_ = 15;                   // カーソルの現在グリッド座標（初期位置は中心）

	// ── シミュレーションのタイマー関連 ──
	float simTimer_ = 0.0f; // 毎フレームのdeltaTimeを累積させる内部タイマー

	// ── 財政の一時保持バッファ ──
	float pendingIncome_ = 0.0f;       // シミュレーション実行時に発生し、GameSceneが未回収のプール資金
	float lastTurnIncome_ = 0.0f;      // UI表示用：直近ターンに発生した純粋な売上
	float lastTurnMaintenance_ = 0.0f; // UI表示用：直近ターンに引かれた維持費総額
	float lastTurnNet_ = 0.0f;         // UI表示用：直近ターンの純収支（売上 - 維持費）

	// ── 満足度伝播計算用のダブルバッファ ──
	// 各セルの満足度を走査しながらリアルタイムで上書きすると、計算順序（左上から右下など）によって
	// 伝播が偏るバグが起きるため、前ターンの確定状態を「前ターン満足度バッファ」に退避させて参照する
	std::vector<std::vector<float>> prevSatisfaction_;

	// ── ログデータ配列 ──
	std::vector<PopLogEntry> popLog_;       // 人口推移履歴
	std::vector<IncomeLogEntry> incomeLog_; // 収支推移履歴
	int prevTotalPop_ = 0;                  // 1つ前のターンの総人口（増減deltaを計算するために使用）
	static const int MAX_LOG = 20;          // ログを保持する最大ターン数（古いものは消去する）

	// ── 内部シミュレーション計算用のサブルーチン関数群 ──
	bool IsAdjacentToRoad(int x, int z);                          // 隣接4方向に道路があるか判定する
	int CountNearbyType(int x, int z, CellType type, int radius); // 指定範囲内に特定の建物がいくつあるか数える
	float AverageNeighborSatisfaction(int x, int z, int radius);  // 周囲の住宅の前ターン満足度平均を計算する

	// ── 老朽化システムのチューニング定数と関数 ──
	static constexpr int AGE_GRACE = 20;        // 新築猶予期間：建設後20ターンまでは劣化しない
	static constexpr float AGE_DECAY = 0.02f;   // 劣化速度：猶予を過ぎた後、1ターンにつき収入効率が2%ずつ低下
	static constexpr float AGE_MIN_EFF = 0.20f; // 劣化下限：どれだけ老朽化しても新築時の20%の生産力は維持する
	float AgeEfficiency(int age) const;

	void SimulateResidential(int x, int z); // 住宅セルの人口増減ロジック
	void SimulateCommercial(int x, int z);  // 商業セルの集客・売上ロジック
	void SimulateIndustrial(int x, int z);  // 工業セルの生産・公害撒き散らしロジック
	void UpdateSatisfaction(int x, int z);  // 住宅セルの満足度計算と近隣伝播
	void UpdateInfluence(int x, int z);     // 全セルの街への影響度計算（ヒートマップ用）
	void RunSimulation();                   // 1ターン分のシミュレーションを回すメイン論理

	void DrawCursor(KamataEngine::PrimitiveDrawer* drawer); // カーソルおよび周囲8マスの枠線を描画する
	void DrawNormal();                                      // 通常モード（建物の高さを反映した3D都市）の描画
	void DrawHeatmap();                                     // ヒートマップモード（フラットカラータイル）の描画
	uint32_t InfluenceToTexture(const Cell& cell);          // 影響度数値から適切なヒートマップ用テクスチャハンドルを返す

public:
	// ── 外部（GameSceneなど）から弄る制御フラグ ──
	bool enableSimulation_ = false;                 // シミュレーションの時計を動かすかどうかのフラグ
	float simInterval_ = 1.0f;                      // 1ターンが経過する時間（秒）
	DisplayMode displayMode_ = DisplayMode::Normal; // 現在の表示モード状態

	CellAutomaton();
	~CellAutomaton();

	void Initialize(KamataEngine::Model* model, KamataEngine::Camera* camera);
	void Update(float deltaTime);
	void Draw(KamataEngine::PrimitiveDrawer* drawer);

	// ── カーソル制御・位置取得 ──
	void MoveCursor(int dx, int dz);
	void GetCursorPosition(int& x, int& z) const {
		x = cursorX_;
		z = cursorZ_;
	}
	// ── 表示モード切替 ──
	void ToggleDisplayMode() { displayMode_ = (displayMode_ == DisplayMode::Normal) ? DisplayMode::Heatmap : DisplayMode::Normal; }

	// ── セルデータの直接変更（建設・破壊・取得） ──
	void PlaceCell(int x, int z, CellType type); // 特定の位置に建物を強制上書き配置
	void PlaceCellAtCursor(CellType type);       // 現在のカーソル位置に配置
	void RemoveCell(int x, int z);               // 建物を消去してEMPTYにする
	Cell* GetCell(int x, int z);                 // 指定位置のセル構造体のポインタを返す（境界チェック付き）
	int GetGridSize() const { return GRID_SIZE; }

	// ── 統計データ取得用ゲッター ──
	int GetTotalPopulation() const;
	float GetAverageSatisfaction() const;
	float GetLastTurnIncome() const { return lastTurnIncome_; }
	float GetLastTurnMaintenance() const { return lastTurnMaintenance_; }
	float GetLastTurnNet() const { return lastTurnNet_; }
	float GetAgeEfficiency(int age) const { return AgeEfficiency(age); }

	// ── 会計用関数 ──
	float CollectIncome(); // GameSceneが予算を受け取るための関数（呼び出し後pendingはゼロリセットされる）

	// ── ログ配列へのアクセス ──
	const std::vector<PopLogEntry>& GetPopLog() const { return popLog_; }
	const std::vector<IncomeLogEntry>& GetIncomeLog() const { return incomeLog_; }
};