#include "WorldTransform.h"
using Matrix4x4 = KamataEngine::Matrix4x4;

void WorldTransformUpdate(KamataEngine::WorldTransform& worldTransform) {
	using namespace KamataEngine::MathUtility; // 行列作成関数を使いやすくする

	// 1. 各行列を個別に作成する
	// scale_, rotation_, translation_ のように最後に「_」がついているはずです
	Matrix4x4 matScale = MakeScaleMatrix(worldTransform.scale_);

	// 回転は X, Y, Z の順番で掛け合わせる
	Matrix4x4 matRot = MakeRotateXMatrix(worldTransform.rotation_.x) * MakeRotateYMatrix(worldTransform.rotation_.y) * MakeRotateZMatrix(worldTransform.rotation_.z);

	Matrix4x4 matTrans = MakeTranslateMatrix(worldTransform.translation_);

	// 2. 行列を合成して代入する（スケーリング * 回転 * 平行移動 の順）
	worldTransform.matWorld_ = matScale * matRot * matTrans;

	// 3. 定数バッファ（GPU）に転送する
	worldTransform.TransferMatrix();
}
