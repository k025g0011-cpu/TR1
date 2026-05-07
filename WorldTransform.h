#pragma once
#include "KamataEngine.h"
#include "WorldTransform.h" // 自身の構造体定義などが必要な場合

/// <summary>
/// 行列を計算・転送する
/// </summary>
void WorldTransformUpdate(KamataEngine::WorldTransform& worldTransform);
