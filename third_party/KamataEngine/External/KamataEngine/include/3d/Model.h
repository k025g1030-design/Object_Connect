#pragma once

#include <3d\LightGroup.h>
#include <3d\Material.h>
#include <3d\Mesh.h>
#include <3d\ObjectColor.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace KamataEngine {

class Camera;
class WorldTransform;

/// <summary>
/// モデル共通データ
/// </summary>
class ModelCommon {
public:
	static ModelCommon* GetInstance();
	static void Terminate();

	// パイプラインセット
	struct PipelineSet {
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
	};

	// カリングモード
	enum class CullingMode {
		kBack = 0, // バックカリング
		kFront,    // フロントカリング
		kNone,     // カリングなし

		kCount,
	};

	// ブレンドモード
	enum class BlendMode {
		kNormal = 0, // 通常αブレンド。Src * SrcA + Dest * (1 - SrcA)
		kNone,       // ブレンドなし。	Src * 1 + Dest * 0
		kAdd,        // 加算。			Src * SrcA + Dest * 1
		kSubtract,   // 減算。			Dest * 1 - Src * SrcA
		kMultiply,   // 乗算。			Src * 0 + Dest * Src
		kScreen,     // スクリーン。	Src * (1 - Dest) + Dest * 1

		kCount,
	};

	// デプステストモード
	enum class DepthTestMode {
		kOn = 0,     // デプステストあり（デプス書き込みあり）
		kOff,    // デプステストなし（デプス書き込みなし）
		kAlwaysPass, // 常にパスする（デプス書き込みあり）
		kReadOnly,   // 読み取り専用（デプス書き込みなし）

		kCount,
	};

	// パイプラインセットを取得するためのキー
	struct PipelineSetKey {
		// カリングモード
		CullingMode cullingMode = CullingMode::kBack;
		BlendMode blendMode = BlendMode::kNormal;
		DepthTestMode depthTestMode = DepthTestMode::kOn;
	};

	struct PipelineSetKeyHash {
		size_t operator()(const PipelineSetKey p) const {
			size_t result = static_cast<size_t>(p.cullingMode) | (static_cast<size_t>(p.blendMode) << 4) | (static_cast<size_t>(p.depthTestMode) << 8);
			return result;
		}
	};

	class PipelineSetKeyEqual {
	public:
		bool operator()(const PipelineSetKey& left, const PipelineSetKey& right) const { 
			return 
				left.cullingMode == right.cullingMode && 
				left.blendMode == right.blendMode &&
				left.depthTestMode == right.depthTestMode; }
	};

	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize();

	/// <summary>
	/// ライドコマンドを積む
	/// </summary>
	void LightCommand(const LightGroup* lightGroup = nullptr);

	/// <summary>
	/// トランスフォームコマンドを積む
	/// </summary>
	/// <param name="worldTransform">ワールドトランスフォーム</param>
	/// <param name="camera">カメラ</param>
	void TransformCommand(const WorldTransform& worldTransform, const Camera& camera);

	/// <summary>
	/// 描画前処理
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	void PreDraw(PipelineSetKey pipelineSetKey, ID3D12GraphicsCommandList* commandList);

	/// <summary>
	/// 描画後処理
	/// </summary>
	void PostDraw();

	/// <summary>
	/// getter
	/// </summary>
	bool IsInitialized() const { return initialized_; }
	bool IsAfterPreDraw() const { return commandList_ != nullptr; }
	ID3D12GraphicsCommandList* GetCommandList() const { return commandList_; }
	ObjectColor* GetObjectColor() const { return defaultObjectColor_.get(); }

private:
	ModelCommon() = default;
	~ModelCommon() = default;
	ModelCommon(ModelCommon&) = delete;
	ModelCommon& operator=(ModelCommon&) = delete;

	/// <summary>
	/// グラフィックスパイプラインの初期化
	/// </summary>
	void InitializeGraphicsPipelines();
	// シェーダーコンパイル
	void CompileShaders();
	// ルートシグネチャの生成
	void CreateRootSignature();

	PipelineSet CreatePipelineSetKey(CullingMode cullingMode, BlendMode blendMode, DepthTestMode depthTestMode) const;

	void CreateAndAddPipelineSet(CullingMode cullingMode, BlendMode blendMode, DepthTestMode depthTestMode);

	// シングルトンインスタンス
	static ModelCommon* sInstance_;

	bool initialized_ = false;

	// デスクリプタサイズ
	UINT descriptorHandleIncrementSize_ = 0u;
	// コマンドリスト
	ID3D12GraphicsCommandList* commandList_ = nullptr;
	// パイプラインセット
	std::unordered_map<PipelineSetKey, PipelineSet, PipelineSetKeyHash, PipelineSetKeyEqual> pipelineSets_;
	// デフォルトライト
	std::unique_ptr<LightGroup> defaultLightGroup_;
	// デフォルトオブジェクトα
	std::unique_ptr<ObjectColor> defaultObjectColor_;
	// 頂点シェーダオブジェクト
	Microsoft::WRL::ComPtr<ID3DBlob> vsBlob_;
	// ピクセルシェーダオブジェクト
	Microsoft::WRL::ComPtr<ID3DBlob> psBlob_;
	// ルートシグネチャ
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
};

/// <summary>
/// モデルデータ
/// </summary>
class Model {
public: // 列挙子
	/// <summary>
	/// ルートパラメータ番号
	/// </summary>
	enum class RoomParameter {
		kWorldTransform, // ワールド変換行列
		kCamera,         // カメラ
		kMaterial,       // マテリアル
		kTexture,        // テクスチャ
		kLight,          // ライト
		kObjectColor,    // オブジェクトアルファ
	};

	using PipelineSetKey = ModelCommon::PipelineSetKey;
	using CullingMode = ModelCommon::CullingMode;
	using BlendMode = ModelCommon::BlendMode;
	using DepthTestMode = ModelCommon::DepthTestMode;

private:
	static const char* kBaseDirectory;
	static const char* kDefaultModelName;

public: // 静的メンバ関数
	/// <summary>
	/// ゲーム中一度だけ実行する初期化
	/// </summary>
	static void StaticInitialize();

	/// <summary>
	/// ゲーム中一度だけ実行する終了処理
	/// </summary>
	static void StaticFinalize();

	/// <summary>
	/// 3Dモデル生成
	/// </summary>
	/// <returns></returns>
	static Model* Create();

	/// <summary>
	/// OBJファイルからメッシュ生成
	/// </summary>
	/// <param name="modelname">モデル名</param>
	/// <param name="modelname">エッジ平滑化フラグ</param>
	/// <returns>生成されたモデル</returns>
	static Model* CreateFromOBJ(const std::string& modelname, bool smoothing = false);

	/// <summary>
	/// 球モデル生成
	/// </summary>
	/// <param name="divisionVertial">垂直方向（緯度）分割数</param>
	/// <param name="divisionHorizontal">水平方向（経度）分割数</param>
	/// <returns>生成されたモデル</returns>
	static Model* CreateSphere(uint32_t divisionVertial = 10, uint32_t divisionHorizontal = 10);

	/// <summary>
	/// 描画前処理
	/// </summary>
	/// <param name="cullingMode">カリングモード</param>
	/// <param name="blendMode">ブレンドモード</param>
	/// <param name="depthTestMode">デプステストモード</param>
	static void PreDraw(CullingMode cullingMode = CullingMode::kBack, BlendMode blendMode = BlendMode::kNormal, DepthTestMode depthTestMode = DepthTestMode::kOn);

	/// <summary>
	/// 描画後処理
	/// </summary>
	static void PostDraw();

public: // メンバ関数
	~Model() = default;

	/// <summary>
	/// 描画
	/// </summary>
	/// <param name="worldTransform">ワールドトランスフォーム</param>
	/// <param name="camera">カメラ</param>
	/// <param name="objectColor">オブジェクトカラー</param>
	void Draw(const WorldTransform& worldTransform, const Camera& camera, const ObjectColor* objectColor = nullptr);

	/// <summary>
	/// 描画（テクスチャ差し替え）
	/// </summary>
	/// <param name="worldTransform">ワールドトランスフォーム</param>
	/// <param name="camera">カメラ</param>
	/// <param name="textureHadle">テクスチャハンドル</param>
	/// <param name="objectColor">オブジェクトカラー</param>
	void Draw(const WorldTransform& worldTransform, const Camera& camera, uint32_t textureHadle, const ObjectColor* objectColor = nullptr);

	/// <summary>
	/// メッシュコンテナを取得
	/// </summary>
	/// <returns>メッシュコンテナ</returns>
	inline const std::vector<std::unique_ptr<Mesh>>& GetMeshes() { return meshes_; }

	/// <summary>
	/// 全マテリアルにアルファ値を設定する
	/// </summary>
	/// <param name="alpha"></param>
	void SetAlpha(float alpha);

	/// <summary>
	/// ライトグループを設定する
	/// </summary>
	/// <param name="lightGroup">ライトグループ</param>
	void SetLightGroup(const LightGroup* lightGroup) { lightGroup_ = lightGroup; }

private: // メンバ変数
	// 名前
	std::string name_;
	// メッシュコンテナ
	std::vector<std::unique_ptr<Mesh>> meshes_;
	// マテリアルコンテナ
	std::unordered_map<std::string, std::unique_ptr<Material>> materials_;
	// デフォルトマテリアル
	std::unique_ptr<Material> defaultMaterial_ = nullptr;
	// ライト
	const LightGroup* lightGroup_ = nullptr;

private: // メンバ関数
	// コンストラクタの外部呼び出しを禁止
	Model() = default;

	/// <summary>
	/// ファイルを読み込んで初期化
	/// </summary>
	/// <param name="modelname">モデル名</param>
	/// <param name="smoothing">エッジ平滑化フラグ</param>
	void InitializeFromFile(const std::string& modelname, bool smoothing = false);

	/// <summary>
	/// 頂点データを渡して初期化
	/// </summary>
	/// /// <param name="vertices">頂点配列</param>
	/// <param name="indices">インデックス配列</param>
	void InitializeFromVertices(const std::vector<Mesh::VertexPosNormalUv>& vertices, const std::vector<uint32_t>& indices);

	/// <summary>
	/// モデル読み込み
	/// </summary>
	/// <param name="modelname">モデル名</param>
	/// <param name="modelname">エッジ平滑化フラグ</param>
	void LoadModel(const std::string& modelname, bool smoothing);

	/// <summary>
	/// マテリアル読み込み
	/// </summary>
	void LoadMaterial(const std::string& directoryPath, const std::string& filename);

	/// <summary>
	/// マテリアル登録
	/// </summary>
	void AddMaterial(std::unique_ptr<Material>& material);

	/// <summary>
	/// テクスチャ読み込み
	/// </summary>
	void LoadTextures();
};

} // namespace KamataEngine