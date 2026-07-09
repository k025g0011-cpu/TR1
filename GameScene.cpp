#define NOMINMAX
#include "GameScene.h"
#include "WorldTransform.h"
#include <2d/ImGuiManager.h>
#include <algorithm>

using namespace KamataEngine;

// 現在プレイヤーが建設用として選択している建物の種類（初期値は道路）
static CellType currentPlacingType = CellType::ROAD;

/// <summary>
/// デストラクタ。動的に確保したメモリを安全に解放し、メモリリークを防ぐ。
/// </summary>
GameScene::~GameScene() {
	delete cellModel_;
	delete cellAutomaton_;
	delete debugCamera_;
}

/// <summary>
/// ゲームシーンの初期化。モデルの読み込み、カメラの俯瞰位置設定、各管理クラスの生成を行う。
/// </summary>
void GameScene::Initialize() {
	// 描画用プリミティブモデルの生成
	cellModel_ = Model::Create();
	// 入力管理クラスのインスタンス取得
	input_ = Input::GetInstance();

	// ゲームカメラの初期設定（都市全体を見渡せる斜め上からの俯瞰アングル）
	camera_.Initialize();
	camera_.translation_ = {15.0f, 20.0f, -5.0f}; // グリッドの中心(15,15)付近を捉える位置
	camera_.rotation_.x = 0.8f;                    // 斜め下を見下ろす角度（ラジアン）
	camera_.UpdateMatrix();                        // 行列の更新

	// 線画描画を行うPrimitiveDrawerにカメラの行列を登録
	PrimitiveDrawer::GetInstance()->SetCamera(&camera_);

	// セルオートマトン（都市シミュレータ）の生成と初期化
	cellAutomaton_ = new CellAutomaton();
	cellAutomaton_->Initialize(cellModel_, &camera_);

	// 開発・プレゼン時にカメラを自由に動かせるデバッグカメラの生成
	debugCamera_ = new DebugCamera(1280, 720);
	debugCamera_->Update();
}

/// <summary>
/// 建設費用の支払いと配置処理。残高が足りない場合は建設を拒絶する。
/// </summary>
bool GameScene::TryBuildCell(int x, int z, CellType type) {
	// インライン関数から対象の建物の「建設費」と「維持費」の構造体を取得
	float cost = GetBuildingCost(type).buildCost;

	// 資金ショートのチェック。所持金が建設コストを下回っていれば建設不可
	if (cityBalance_ < cost) {
		return false;
	}


	// 資金を支払い、実際にセルオートマトンのグリッドへデータを書き込む
	cityBalance_ -= cost;
	cellAutomaton_->PlaceCell(x, z, type);
	return true;
}

/// <summary>
/// フレーム毎の更新処理。入力の監視、配置転換、シミュレーションのステップ進行、ImGuiのデータ構築を行う。
/// </summary>
void GameScene::Update() {
	// ── 1. 矢印キーによるカーソル移動処理（トリガー判定で1回押しに対応） ──
	if (input_->TriggerKey(DIK_UP)) {
		cellAutomaton_->MoveCursor(0, 1); // 奥へ移動 (Z+)
	}
		
	if (input_->TriggerKey(DIK_DOWN)) {
		cellAutomaton_->MoveCursor(0, -1); // 手前へ移動 (Z-)
	}
	
	if (input_->TriggerKey(DIK_LEFT)) {
		cellAutomaton_->MoveCursor(-1, 0); // 左へ移動 (X-)
	}
		
	if (input_->TriggerKey(DIK_RIGHT)) {
		cellAutomaton_->MoveCursor(1, 0); // 右へ移動 (X+)
	}
	

	// ── 2. 数字キーによる建物の建設・撤去処理 ──
	int cx, cz;
	cellAutomaton_->GetCursorPosition(cx, cz); // 現在のカーソル位置(X, Z)を取得

	// 建設を簡潔に行うためのラムダ式（クロージャ）を定義
	//	[&]は「この関数の外側にあるローカル変数（cx や cz など）を、そのままこの中へ持ち込んで使いますよ」という合図
	auto tryPlace = [&](CellType type) {
		currentPlacingType = type;  // UI表示用に選択中のタイプを更新
		TryBuildCell(cx, cz, type); // 資金チェック付き建設を呼び出し
	};

	// キー対応: 1=道路, 2=住宅, 3=商業, 4=工業, 5=公園, 0=撤去(空地にする)
	if (input_->TriggerKey(DIK_1))
		tryPlace(CellType::ROAD);
	if (input_->TriggerKey(DIK_2))
		tryPlace(CellType::RESIDENTIAL);
	if (input_->TriggerKey(DIK_3))
		tryPlace(CellType::COMMERCIAL);
	if (input_->TriggerKey(DIK_4))
		tryPlace(CellType::INDUSTRIAL);
	if (input_->TriggerKey(DIK_5))
		tryPlace(CellType::PARK);
	if (input_->TriggerKey(DIK_0))
		cellAutomaton_->RemoveCell(cx, cz); // 撤去は完全無料

	// ── 3. 表示モードの切り替え ──
	if (input_->TriggerKey(DIK_TAB))
		cellAutomaton_->ToggleDisplayMode(); // Tabキーで通常(3D)とヒートマップを反転

	/*
	// ── 4. デバッグカメラの更新と行列の同期 ──
	// ImGuiのウィンドウ上をマウス操作している時は、ゲーム内のカメラが動かないようにガード
	if (!ImGui::GetIO().WantCaptureMouse)
		debugCamera_->Update();

	// デバッグカメラのビュー行列・プロジェクション行列をメインカメラへコピーして適用
	camera_.matView = debugCamera_->GetCamera().matView;
	camera_.matProjection = debugCamera_->GetCamera().matProjection;
	camera_.TransferMatrix(); // GPU（定数バッファ）へ行列データを転送
	*/
	// 代わりにメインカメラ自身の行列更新と転送を呼ぶ
	camera_.UpdateMatrix();
	camera_.TransferMatrix();

	// ── 5. シミュレーションのステップ進行と税収・維持費の会計処理 ──
	cellAutomaton_->Update(deltaTime_); // deltaTime_がないと1フレーム1ターン進んでしまうで

	// 1ターン（ステップ）が経過したフレームのみ、CollectIncome()から「純収益（収入-維持費）」が返ってくる
	// 毎フレームの二重加算が起きない安全な設計になっている
	cityBalance_ += cellAutomaton_->CollectIncome();

	// ── 6. ImGui描画情報の構築開始 ──
	ImGuiManager::GetInstance()->Begin();

	// 満足度の値に応じて文字色を決定する即席ラムダ式 (70以上=緑, 40以上=黄, それ未満=赤)
	auto satColorOf = [](float v) { return v >= 70.0f ? ImVec4(0, 1, 0, 1) : v >= 40.0f ? ImVec4(1, 1, 0, 1) : ImVec4(1, 0, 0, 1); };

	// UIテキスト用の文字列配列と、対応するUI識別カラーの配列
	const char* typeNames[] = {"Empty", "Road", "Residential", "Commercial", "Industrial", "Park"};
	const ImVec4 typeColors[] = {
	    {0.5f, 0.5f, 0.5f, 1},
        {0.6f, 0.6f, 0.6f, 1},
        {0.2f, 0.9f, 0.2f, 1},
        {0.2f, 0.2f, 0.9f, 1},
        {0.9f, 0.6f, 0.1f, 1},
        {0.1f, 0.8f, 0.1f, 1}
    };

	// =================================================================
	// ① City Dashboard ウィンドウ (都市全体の主ステータス)
	// =================================================================
	ImGui::Begin("City Dashboard");

	float avgSat = cellAutomaton_->GetAverageSatisfaction(); // 都市全体の平均住民満足度
	int pop = cellAutomaton_->GetTotalPopulation();          // 都市全体の総住民人口

	// 財政セクション
	ImGui::Text("=== Finance ===");
	ImGui::TextColored(ImVec4(1, 1, 0, 1), "Balance: $%.0f", cityBalance_);       // 現在の国家予算
	//ImGui::Text("Income/turn:  $%.1f", cellAutomaton_->GetLastTurnIncome());      // 前ターンの総収入
	//ImGui::Text("Upkeep/turn:  $%.1f", cellAutomaton_->GetLastTurnMaintenance()); // 前ターンの総維持費
	{
		float net = cellAutomaton_->GetLastTurnNet(); // 純収支（黒字ならプラス、赤字ならマイナス表記）
		ImVec4 netCol = net > 0.0f ? ImVec4(0, 1, 0, 1) : net < 0.0f ? ImVec4(1, 0.3f, 0.3f, 1) : ImVec4(0.8f, 0.8f, 0.8f, 1);
		//ImGui::TextColored(netCol, "Net/turn:     $%+.1f", net);
	}
	if (cityBalance_ <= 0.0f)
		ImGui::TextColored(ImVec4(1, 0, 0, 1), "No funds! (財政破綻状態)");

	ImGui::Separator(); // 区切り線

	// 満足度セクション
	ImGui::Text("=== Satisfaction ===");
	ImGui::TextColored(satColorOf(avgSat), "Avg: %.0f / 100", avgSat);
	ImGui::ProgressBar(avgSat / 100.0f, ImVec2(-1, 0), ""); // 視覚的なプログレスバー表示

	ImGui::Separator();

	// 人口セクション
	ImGui::Text("=== Population ===");
	ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.f, 1), "Total: %d", pop);

	ImGui::Separator();
	// シミュレーションの一時停止・再開を切り替えるチェックボックス
	ImGui::Checkbox("Enable Simulation", &cellAutomaton_->enableSimulation_);
	// 1ターンの長さを秒単位で変更するシークバー (0.2秒〜3.0秒の間)
	ImGui::SliderFloat("Speed (s)", &cellAutomaton_->simInterval_, 0.2f, 3.0f);

	ImGui::End();

	// =================================================================
	// ② Population Log ウィンドウ (直近15ターン分の人口動態グラフ代替データ)
	// =================================================================
	ImGui::Begin("Population Log");

	const auto& log = cellAutomaton_->GetPopLog();
	if (log.empty()) {
		ImGui::TextDisabled("No data. Enable Simulation.");
	} else {
		ImGui::Text("Turn   Total    Change");
		ImGui::Separator();
		int shown = 0;
		// 最新のログから遡って最大15件表示するループ
		for (int i = (int)log.size() - 1; i >= 0 && shown < 15; --i, ++shown) {
			const PopLogEntry& e = log[i];
			ImVec4 col = e.delta > 0 ? ImVec4(0, 1, 0, 1) : e.delta < 0 ? ImVec4(1, 0.3f, 0.3f, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1);
			ImGui::TextColored(col, "T-%2d   %-6d  %+d", (int)log.size() - i, e.total, e.delta);
		}
	}
	ImGui::End();

	// =================================================================
	// ③ Income Log ウィンドウ (産業ごとの内訳と収支の履歴)
	// =================================================================
	ImGui::Begin("Income Log");

	const auto& incLog = cellAutomaton_->GetIncomeLog();
	if (incLog.empty()) {
		ImGui::TextDisabled("No data. Enable Simulation.");
	} else {
		ImGui::Text("Turn  Com  Upkeep  Net");
		ImGui::Separator();
		int shownInc = 0;
		// 最新のログから遡って最大15件表示
		for (int i = (int)incLog.size() - 1; i >= 0 && shownInc < 15; --i, ++shownInc) {
			const IncomeLogEntry& e = incLog[i];
			ImVec4 col = e.net > 0.0f ? ImVec4(0, 1, 0, 1) : e.net < 0.0f ? ImVec4(1, 0.3f, 0.3f, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1);
			ImGui::TextColored(col, "T-%2d  %-5.0f  %-6.0f  %+.0f", (int)incLog.size() - i, e.commercial, e.maintenance, e.net);
		}
	}
	ImGui::End();

	// =================================================================
	// ④ View ウィンドウ (表示モードの管理とカラー凡例の提示)
	// =================================================================
	ImGui::Begin("View");

	bool isHeatmap = (cellAutomaton_->displayMode_ == DisplayMode::Heatmap);
	ImGui::Text("Display Mode");
	// ラジオボタンによる表示モードの切り替え
	if (ImGui::RadioButton("Buildings", !isHeatmap)) {
		cellAutomaton_->displayMode_ = DisplayMode::Normal;
	}
	ImGui::SameLine(); // 横並びにする
	if (ImGui::RadioButton("Heatmap", isHeatmap)) {
		cellAutomaton_->displayMode_ = DisplayMode::Heatmap;
	}

	// ヒートマップが有効な場合のみ、プレイヤーに視覚的なカラーチャートを表示
	if (isHeatmap) {
		ImGui::Separator();
		ImGui::Text("Influence on the city:");
		ImGui::TextColored(ImVec4(0.1f, 0.8f, 0.1f, 1), "  Green  : Very good (大繁栄の住宅街)");
		ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.2f, 1), "  Yellow : Good      (安定した住宅街)");
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1), "  White  : Neutral   (普通の住宅/空き地)");
		ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1), "  Red    : Bad       (道路未接続 / 近隣に公害)");
		ImGui::TextColored(ImVec4(0.7f, 0.3f, 0.9f, 1), "  Purple : Worst     (重度の工業公害まみれ)");
		ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1), "  Black  : Non-residential building (インフラ・産業枠)");
		ImGui::TextDisabled("  (※真上から見下ろすと綺麗に見えます)");
	}
	ImGui::End();

	// =================================================================
	// ⑤ Controls ウィンドウ (操作説明およびカーソル直下の詳細セルインスペクタ)
	// =================================================================
	ImGui::Begin("Controls");

	// 現在選択している（スロットに入っている）建設予定の建物名
	ImGui::Text("Selected:");
	ImGui::SameLine();
	ImGui::TextColored(typeColors[static_cast<int>(currentPlacingType)], "%s", typeNames[static_cast<int>(currentPlacingType)]);

	// 建設費と操作キーの対応（折りたたみヘッダー）
	if (ImGui::CollapsingHeader("Build / Keys")) {
		CellType types[] = {CellType::ROAD, CellType::RESIDENTIAL, CellType::COMMERCIAL, CellType::INDUSTRIAL, CellType::PARK};
		int keys[] = {1, 2, 3, 4, 5};
		for (int i = 0; i < 5; ++i) {
			auto cost = GetBuildingCost(types[i]);
			ImGui::TextColored(typeColors[static_cast<int>(types[i])], "%d:%-12s $%.0f (up $%.0f)", keys[i], typeNames[static_cast<int>(types[i])], cost.buildCost, cost.maintCost);
		}
		ImGui::Text("0  : Remove (free)");
		ImGui::Text("Tab: Toggle heatmap");
		ImGui::Text("Arrow: Move Cursor");
	}

	// デバッグカメラのマウス操作方法（折りたたみヘッダー）
	if (ImGui::CollapsingHeader("Camera")) {
		ImGui::Text("Left drag : Rotate (カメラ回転)");
		ImGui::Text("Wheel     : Zoom (拡大縮小)");
		ImGui::Text("Mid drag  : Pan (平行移動)");
	}

	// 【最重要】カーソル直下にあるマスの詳細なステータス監視 (デフォルトで開いた状態)
	if (ImGui::CollapsingHeader("Cell Info", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Cursor: (%d, %d)", cx, cz);
		Cell* cell = cellAutomaton_->GetCell(cx, cz);

		if (cell && cell->type != CellType::EMPTY) {
			// 建物の種類を表示
			ImGui::TextColored(typeColors[static_cast<int>(cell->type)], "%s ", typeNames[static_cast<int>(cell->type)]);

			// 住宅なら「住民人口」、商業・工業なら現在の「顧客数・稼働率」として解釈し表示
			if (cell->type == CellType::RESIDENTIAL) {
				ImGui::Text("Population: %d", cell->population);
			} else {
				ImGui::Text("Customer/Output: %d", cell->population);
			}
			//ImGui::Text("maintenance/turn: $%.1f", GetBuildingCost(cell->type).maintCost);

			// 商業・工業に特有の「経営データ（収入と経年劣化）」の表示
			if (cell->type == CellType::COMMERCIAL || cell->type == CellType::INDUSTRIAL) {
				//ImGui::Text("Income/turn: $%.1f", cell->income);

				// 老朽化に伴う生産・集客効率の減退率を算出
				float eff = cellAutomaton_->GetAgeEfficiency(cell->age);
				ImVec4 effCol = eff >= 0.99f ? ImVec4(0, 1, 0, 1) : eff >= 0.6f ? ImVec4(1, 1, 0, 1) : ImVec4(1, 0.3f, 0.3f, 1);
				ImGui::Text("Age: %d turns", cell->age);
				ImGui::TextColored(effCol, "Efficiency: %.0f%%", eff * 100.0f);
				
			}

			/*
			// 商業特有の「工業連携シナジー（仕入れルート補正）」の表示
			if (cell->type == CellType::COMMERCIAL) {
				int factories = 0;
				// 周囲半径4マスの範囲にある工業の数を数え上げる
				for (int dx = -4; dx <= 4; ++dx)
					for (int dz = -4; dz <= 4; ++dz) {
						if (dx == 0 && dz == 0)
							continue;
						Cell* n = cellAutomaton_->GetCell(cx + dx, cz + dz);
						if (n && n->type == CellType::INDUSTRIAL)
							factories++;
					}
				// 工業1つにつきキャパシティ上限+50% (最大+150%) のボーナス値を可視化
				ImGui::Text("Nearby factories: %d (+%.0f%% cap)", factories, std::min(1.5f, factories * 0.5f) * 100.0f);
			}
			*/
			// 住宅特有の「住民満足度」バー表示
			if (cell->type == CellType::RESIDENTIAL) {
				float sat = cell->satisfaction;
				ImGui::TextColored(satColorOf(sat), "Satisfaction: %.0f", sat);
				ImGui::ProgressBar(sat / 100.0f, ImVec2(-1, 0), "");
			}

			// 道路インフラへのアクセス状況チェック（上下左右に隣接しているか）
			const int dx[] = {0, 0, 1, -1}, dz[] = {1, -1, 0, 0};
			bool hasRoad = false;
			for (int i = 0; i < 4; ++i) {
				Cell* n = cellAutomaton_->GetCell(cx + dx[i], cz + dz[i]);
				if (n && n->type == CellType::ROAD) {
					hasRoad = true;
					break;
				}
			}
			ImGui::TextColored(hasRoad ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1), hasRoad ? "Road: Connected" : "Road: NOT connected");
		} else {
			ImGui::TextDisabled("(empty / さら地)");
		}
	}
	ImGui::End();

	// ImGuiの描画データ構築終了
	ImGuiManager::GetInstance()->End();
}

/// <summary>
/// 描画パイプラインの実行。3Dオブジェクト、地形のグリッド線、およびImGuiのガジェットを描画する。
/// </summary>
void GameScene::Draw() {
	// 3Dオブジェクト描画の事前設定
	Model::PreDraw();

	PrimitiveDrawer* pd = PrimitiveDrawer::GetInstance();

	// セルオートマトン側に登録された全セルの3Dモデル（またはヒートマップタイル）を一括描画
	cellAutomaton_->Draw(pd);

	// ── 30x30マスの白い外枠と、灰色のグリッド補助線を床に引く処理 ──
	const float gs = 30.0f, y = 0.05f; // 地面より少しだけ高い位置(Y=0.05)に浮かせて描画

	// 外周の太い白枠
	pd->DrawLine3d({0, y, 0}, {gs, y, 0}, {1, 1, 1, 1});
	pd->DrawLine3d({gs, y, 0}, {gs, y, gs}, {1, 1, 1, 1});
	pd->DrawLine3d({gs, y, gs}, {0, y, gs}, {1, 1, 1, 1});
	pd->DrawLine3d({0, y, gs}, {0, y, 0}, {1, 1, 1, 1});

	// 内側のマス目区切り線（薄い半透明の灰色）
	for (int i = 1; i < 30; ++i) {
		float p = static_cast<float>(i);
		pd->DrawLine3d({0, y, p}, {gs, y, p}, {0.5f, 0.5f, 0.5f, 0.6f}); // 横線
		pd->DrawLine3d({p, y, 0}, {p, y, gs}, {0.5f, 0.5f, 0.5f, 0.6f}); // 縦線
	}

	// 3Dオブジェクト描画の事後処理
	Model::PostDraw();

	// メインループの最後にImGuiマネージャを駆動し、構築したUIを2D最前面に焼き付ける
	ImGuiManager::GetInstance()->Draw();
}