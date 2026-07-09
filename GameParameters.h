#pragma once

struct GameParameters {
	// ── 建設コスト ──
	float costRoad = 100.0f;
	float costResidential = 300.0f;
	float costCommercial = 500.0f;
	float costIndustrial = 400.0f;

	// ── 維持費 ──
	float maintCommercial = 4.0f;
	float maintIndustrial = 2.0f;

	// ── シミュレーション定数（老朽化関連） ──
	int ageGrace = 20;       // 新築猶予期間
	float ageDecay = 0.1f;   // 劣化速度
	float ageMinEff = 0.20f; // 劣化下限
};