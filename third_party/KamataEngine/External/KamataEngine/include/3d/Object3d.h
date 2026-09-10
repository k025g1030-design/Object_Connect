#pragma once
#include <3d/WorldTransform.h>

namespace KamataEngine {

class Camera;
class Model;

// 3Dオブジェクト
class Object3d {
public:
	// 描画前処理
	static void PreDraw(Camera* camera = nullptr, Model::CullingMode cullingMode = Model::CullingMode::kBack, Model::BlendMode blendMode = Model::BlendMode::kNormal);

	// 描画後処理
	static void PostDraw();

	// アクセサ
	static void SetCamera(Camera* camera);
	static Camera* GetCamera();

	// 初期化
	virtual void Initialize(Model* model);

	// 更新
	virtual void Update();

	// 描画
	virtual void Draw();
	virtual void Draw(const Camera& camera);

	// 移動
	void Translate(const Vector3& move);
	// 回転
	void Rotate(const Vector3& rotate);
	// ワールド行列によるベクトルの回転
	Vector3 TransformVector(const Vector3& v);
	// ワールド行列による座標変換
	Vector3 TransformPoint(const Vector3& v);

	// アクセサ
	void SetModel(Model* model) { model_ = model; }
	Model* GetModel() const { return model_; }
	WorldTransform& GetWorldTransform() { return worldTransform_; }
	Matrix4x4& GetWorldMatrix() { return worldTransform_.matWorld_; }
	void SetTranslation(const Vector3& translation) { worldTransform_.translation_ = translation; }
	void SetRotation(const Vector3& rotation) { worldTransform_.rotation_ = rotation; }
	void SetScale(const Vector3& scale) { worldTransform_.scale_ = scale; }
	const Vector3& GetTranslation() const { return worldTransform_.translation_; }
	const Vector3& GetRotation() const { return worldTransform_.rotation_; }
	const Vector3& GetScale() const { return worldTransform_.scale_; }
	void SetParentWorldTransform(const WorldTransform* parent) { worldTransform_.parent_ = parent; }
	const WorldTransform* GetParentWorldTransform() const { return worldTransform_.parent_; }

protected:
	// ワールドトランスフォーム
	WorldTransform worldTransform_;
	// モデル
	Model* model_ = nullptr;
};

} // namespace KamataEngine
