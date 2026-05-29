#pragma once
#include "raylib.h"
#include <vector>
#include "Timer.h"
#include "BaseModel.h"
#include "PhysicsWorld.h"
#include <memory>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include "Customer.h"
#include "raydial.h"
#include <unordered_map>
#include <string>



#if defined(_DEBUG) && !defined(__EMSCRIPTEN__)
#define GAME_ENABLE_EDITOR 1
#else
#define GAME_ENABLE_EDITOR 0
#endif

enum LightType
{
	LIGHT_DIRECTIONAL = 0,
	LIGHT_POINT,
	LIGHT_SPOT
};



struct Light
{

	std::string name = "Light";
	int type = 0;
	int enabled = 0;
	Vector3 position{ 0.0f, 0.0f, 0.0f };
	Vector3 target{ 0.0f, 0.0f, 0.0f };
	float color[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
	float intensity = 0.0f;


	int typeLoc = -1;
	int enabledLoc = -1;
	int positionLoc = -1;
	int targetLoc = -1;
	int colorLoc = -1;
	int intensityLoc = -1;
	int rangeLoc = -1;

	bool castsShadow = 0;
	float range = 6.0f;

	float shadowRange = 6.0f;

	float shadowBias = 0.003f;
	float shadowStrength = 0.55f;
	float shadowOrthoSize = 8.0f;
	float shadowNear = 0.1f;
	float shadowFar = 30.0f;
	float shadowFovy = 45.0f;
};



enum class EditorItemType
{
	Floor,
	Prop,
	Light,
	Blockout,
	PhysicsBox,
	SceneProp
};

struct EditorItem
{
	EditorItemType type = EditorItemType::Floor;
	int index = -1;

	std::string* name = nullptr;
	Vector3* position = nullptr;
	Vector3* rotation = nullptr;
	Vector3* size = nullptr;

	bool* visible = nullptr;
	bool* hasCollision = nullptr;
	bool* useLegacyBoxCollision = nullptr;
	bool* useJoltCollider = nullptr;

	float* intensity = nullptr;
};

enum class BodyType
{
	Static,    // walls, counter, shelves, map geometry
	Dynamic,   // boxes you can throw
	Kinematic  // player, moving doors, scripted movers
};
struct AABB
{
	Vector3 min;
	Vector3 max;
};

struct PhysicsBody
{
	BodyType type = BodyType::Static;

	Vector3 position{ 0,0,0 };
	Vector3 velocity{ 0,0,0 };
	Vector3 halfExtents{ 0.5f, 0.5f, 0.5f };
	Vector3 rotationDeg{ 0.0f, 0.0f, 0.0f };
	bool useJoltCollider = false;
	bool blocksPlayer = true;

	float invMass = 0.0f;     // 0 for static
	bool useGravity = false;
	bool isTrigger = false;
	bool isActive = true;
};
class Game
{
public:
	Game();
	~Game();
	void run();
	void draw();
	void update();
	void cameraPlayerPos();
	void DrawLevel(void) const;
	void updateCameraFPS();
	void updateCameraController(float dt);

	struct RecordPlayerTrackInfo
	{
		std::string artist = "Unknown Artist";
		std::string title = "Unknown Track";
		std::string album = "";
		std::string coverImagePath = "";

		std::vector<unsigned char> coverBytes;
		std::string coverMime = "";
		bool hasEmbeddedCover = false;
	};

#ifdef __EMSCRIPTEN__
	void RegisterWebCallbacks();
	void OnWebFocusLost();
	void OnWebFocusReturned();
	void OnWebPointerLockChanged(bool locked);
#endif

private:
	void importAssets();
	void setLighting();
	void createTestScene();
	void updatePBRShader();

	void DrawStartupLoadingScreen(const char* status, float progress, double startTime);
	void LoadingPulse(const char* status, float progress, bool forceDraw = false);



	void FinishLoading();
	Light CreateLight(int type, Vector3 position, Vector3 target, Color color, float intensity);
	void UpdateLight(const Light& light) const;

	Camera3D camera;
	Player player;

	Vector3 playerSpawnPosition{ -1.5f, 0.0f, 8.0f };
	float playerSpawnYaw = 0.0f;

	void ResetPlayerToSpawn();

	Shader pbrShader{};

	int metallicValueLoc = -1;
	int roughnessValueLoc = -1;
	int emissiveIntensityLoc = -1;
	int emissiveColorLoc = -1;
	int textureTilingLoc = -1;
	int lightCountLoc = -1;
	int albedoColorLoc = -1;
	int aoValueLoc = -1;
	int normalValueLoc = -1;
	int offsetLoc = -1;

	int pbrUseTexAlbedoLoc = -1;
	int pbrUseTexNormalLoc = -1;
	int pbrUseTexMRALoc = -1;
	int pbrUseTexEmissiveLoc = -1;

	int pbrUseTexMetallicLoc = -1;
	int pbrUseTexRoughnessLoc = -1;
	int pbrUseTexAOLoc = -1;
	int pbrUseGltfMetallicRoughnessLoc = -1;

	int pbrAlphaModeLoc = -1;
	int pbrAlphaCutoffLoc = -1;
	int pbrReceiveShadowsLoc = -1;

	int reflectionStrengthLoc = -1;
	float reflectionStrength = 0.35f;

	void ApplyEnvironmentCubemap(Model& model);

	void RepairAnimatedCustomerMaterialTextures(Model& model);



	std::vector<unsigned char> frustumVisibleNowScratch;
	std::vector<unsigned char> occluderDrawnNowScratch;

	Shader instancedPbrShader{};

	int instancedTilingLoc = -1;
	int instancedOffsetLoc = -1;

	int instancedMetallicValueLoc = -1;
	int instancedRoughnessValueLoc = -1;
	int instancedAoValueLoc = -1;
	int instancedNormalValueLoc = -1;
	int instancedEmissivePowerLoc = -1;
	int instancedReflectionStrengthLoc = -1;

	int instancedAlbedoColorLoc = -1;
	int instancedEmissiveColorLoc = -1;

	int instancedAlphaModeLoc = -1;
	int instancedAlphaCutoffLoc = -1;
	int instancedReceiveShadowsLoc = -1;

	int instancedUseTexAlbedoLoc = -1;
	int instancedUseTexNormalLoc = -1;
	int instancedUseTexMRALoc = -1;
	int instancedUseTexEmissiveLoc = -1;

	int instancedUseTexMetallicLoc = -1;
	int instancedUseTexRoughnessLoc = -1;
	int instancedUseTexAOLoc = -1;
	int instancedUseGltfMetallicRoughnessLoc = -1;
	//blur loc
	int blurScaleLoc{};
	float inspectBlurScale = 2.0f;
	//models
	Model pbrFloor{};
	Model testProp{};
	Model gBoy{};
	Model Shelf{};

	Model japanStoreStaticModel{};
	bool japanStoreStaticLoaded = false;

	void LoadJapanStoreStatic();
	void DrawJapanStoreStatic() const;
	void UnloadJapanStoreStatic();

	struct InstancedModelBatch
	{
		Model model{};
		bool loaded = false;
		std::vector<Matrix> transforms;

		// New: per-mesh alpha settings for instanced rendering
		std::vector<int> meshAlphaModes;          // 0 opaque, 1 mask, 2 blend
		std::vector<float> meshAlphaCutoffs;
		std::vector<float> meshBaseAlphas;
		std::vector<float> meshReflectionStrengths;

		// Optional manual override if automatic alpha detection fails
		std::vector<int> transparentMaterialIndices;

	};

	mutable std::vector<Matrix> visibleInstancedTransformsScratch;

	const std::vector<Matrix>& GetVisibleInstancedTransforms(
		const InstancedModelBatch& batch,
		const Camera3D& cam,
		float approximateRadius,
		float maxDistance
	) const;

	void ApplyPBRMaterialUniformsForMesh(
		const Material& mat,
		const Mesh& mesh,
		int alphaMode,
		float alphaCutoff,
		float baseAlpha,
		float reflectionStrengthValue,
		bool cheapOpaqueMode = false
	) const;

	void DrawInstancedModelBatchPBRVisible(
		const InstancedModelBatch& batch,
		const Camera3D& cam,
		bool transparentPass,
		const std::vector<Matrix>& visibleTransforms
	) const;


	mutable std::vector<Matrix> visibleGachaBallTransformsScratch;

	void BuildVisibleGachaBallTransforms(const Camera3D& cam) const;

	InstancedModelBatch gachaInstanceBatch;
	Shader instancingShader{};

	void PrepareInstancedPBRShader(const Camera3D& cam) const;
	void ApplyInstancedPBRMaterialUniforms(
		const Material& mat,
		const Mesh& mesh,
		int alphaMode,
		float alphaCutoff,
		float baseAlpha,
		float reflectionStrengthValue,
		bool cheapOpaqueMode = false
	) const;
	void BuildInstancedBatchAlphaSettings(InstancedModelBatch& batch);
	void DrawGachaMachineInstanceTest(const Camera3D& cam) const;
	void DrawGachaBallInstances(const Camera3D& cam) const;

	void DrawInstancedModelBatchPBR(
		const InstancedModelBatch& batch,
		const Camera3D& cam,
		bool transparentPass,
		const std::vector<Matrix>* overrideTransforms = nullptr
	) const;

	bool InstancedBatchHasTransparentMeshes(const InstancedModelBatch& batch) const;

	void LoadGachaMachineInstanceTest();
	void UnloadGachaMachineInstanceTest();


	Texture2D floorAlbedo{};
	Texture2D floorNormal{};
	Texture2D floorMRA{};

	Texture2D shelfAlbedo{};
	Texture2D shelfNormal{};
	Texture2D shelfMRA{};

	Texture2D gBoyAlbedo{};
	Texture2D gBoyNormal{};
	Texture2D gBoyMRA{};


	Texture2D neutralNormalTexture{};
	Texture2D defaultGltfMRTexture{};

	Vector2 floorTextureTiling = { 4.0f, 4.0f };
	Vector2 propTextureTiling = { 1.0f, 1.0f };

	Light lights[MAX_LIGHTS]{};
	int lightCount = 0;

	float headTimer = 0.0f;
	float walkLerp = 0.0f;
	float headLerp = STAND_HEIGHT;
	Vector2 lean = { 0.0f, 0.0f };

	struct BlockoutBox
	{
		std::string name = "Blockout";
		Vector3 position;
		Vector3 size;
		Vector3 rotationDeg{ 0.0f, 0.0f, 0.0f };
		Color color;
		bool hasCollision = true;
		bool blocksPlayer = true;
		bool visible = true;
		bool blocksCustomers = true;

		bool useNormalCollision = false; // legacy AABB collision
		bool useJoltCollider = true;     // Jolt static body
	};
	struct TextureUsageFlags
	{
		int albedo = 0;
		int normal = 0;
		int mra = 0;
		int emissive = 0;
	};
	struct PlacementHit
	{
		bool valid = false;
		float distance = 0.0f;
		Vector3 point{ 0.0f, 0.0f, 0.0f };
	};


	enum class CustomerPOIKind
	{
		Generic = 0,
		Entry = 1,
		Exit = 2,
		BrowseItem = 3,
		Counter = 4,
		QueueSlot = 5
	};

	struct CustomerPOI
	{
		std::string id = "poi";
		std::string group = "any";

		Vector3 position{ 0.0f, 0.0f, 0.0f };

		float radius = 0.35f;

		float waitSecondsMin = 1.0f;
		float waitSecondsMax = 3.0f;

		bool enabled = true;
		bool stopPoint = true;

		bool exclusive = true;
		int capacity = 1;

		CustomerPOIKind kind = CustomerPOIKind::Generic;
		int queueOrder = -1;

		// New
		bool useFacingDirection = false;
		float facingYawDeg = 0.0f;
	};

	enum class ItemPlacementSpotKind
	{
		CounterOffer = 0,
		CounterScan = 1,
		ShelfSlot = 2
	};

	struct ItemPlacementSpot
	{
		std::string id = "spot";

		ItemPlacementSpotKind kind = ItemPlacementSpotKind::ShelfSlot;

		Vector3 position{ 0.0f, 0.0f, 0.0f };
		Vector3 rotationDeg{ 0.0f, 0.0f, 0.0f };
		Vector3 scale{ 1.0f, 1.0f, 1.0f };

		float snapRadius = 0.55f;

		bool enabled = true;
		bool allowPlayerDrop = true;
		bool allowCustomerPlace = true;

		int occupiedScenePropIndex = -1;

		// Optional later: "gameboy", "cartridge", "phone", etc.
		std::string acceptedItemTag = "";
	};

	enum class PickupItemCategory
	{
		Generic = 0,
		ForSale = 1,      // Browser/buyer can buy this from shelf
		Selling = 2,      // Seller can bring/sell this to player
		Both = 3
	};

	struct CustomerTradeItemDef
	{
		std::string id = "";
		std::string displayName = "";

		Model* model = nullptr;

		std::string itemTag = "trade_item";

		Vector3 scale{ 1.0f, 1.0f, 1.0f };

		Vector3 holdOffsetLocal{ 0.0f, -0.15f, -0.15f };
		Vector3 holdRotationOffsetDeg{ 0.0f, 90.0f, 0.0f };
		bool holdFollowCameraPitch = true;

		Vector3 dropRotationOffsetDeg{ 0.0f, -90.0f, 0.0f };

		int baseOfferYen = 3000;

		int sellPriceYen = 0;

		// Which customer role is allowed to use this item.
		bool availableFromSellers = true; // Seller brings this to sell to the shop
		bool availableForBuyers = true;   // Browser/buyer can buy this from the shop

		std::vector<std::string> inspectLines;

		std::string sellerIntroLine = "";
		std::string sellerConditionLine = "";
		std::string sellerPriceHopeLine = "";
		std::string buyerCounterLine = "";


	};

	std::vector<CustomerTradeItemDef> customerTradeItems;

	void BuildCustomerTradeItemCatalog();

	const CustomerTradeItemDef* FindCustomerTradeItemDef(
		const std::string& itemId
	) const;

	const CustomerTradeItemDef* PickRandomCustomerTradeItemDef() const;

	const CustomerTradeItemDef* PickRandomTradeItemDefForCustomer(
		bool seller
	) const;

	int startingBudgetYen = 10000;
	int storeBudgetYen = 10000;

	int lastTransactionDeltaYen = 0;
	std::string lastTransactionText = "";

	int GetSellerBuyPriceYen(const CustomerTradeItemDef& item) const;
	int GetBuyerSellPriceYen(const CustomerTradeItemDef& item) const;

	int GetSellerBuyPriceYenForCustomer(int customerIndex) const;
	int GetBuyerSellPriceYenForCustomer(int customerIndex) const;

	void ApplyBuyerPurchaseTransaction(int customerIndex);
	void ApplySellerPurchaseTransaction(int customerIndex);

	void CustomerTakeCounterItem(int customerIndex);

	void DrawBudgetHUD() const;



	bool DoesPickupCategoryMatchCustomer(
		PickupItemCategory category,
		bool seller
	) const;

	int FindTradeItemTemplateSceneProp(
		const std::string& tradeItemId,
		bool seller
	) const;

	void AssignTradeItemToCustomer(
		int customerIndex,
		bool seller
	);

	int CreateTradeItemScenePropForCustomer(
		int customerIndex,
		ItemPlacementSpotKind spotKind
	);

	bool IsPlacementSpotOccupiedByValidProp(
		int spotIndex,
		int allowedPropIndex = -1
	) const;

	bool EnsureCustomerCounterItemPlaced(int customerIndex);

	std::string GetSellerScriptIdForItem(
		const std::string& itemId,
		const std::string& part
	) const;

	std::string GetBuyerCounterScriptIdForItem(
		const std::string& itemId
	) const;

	void AddSellerDialogueScriptsForItem(
		const CustomerTradeItemDef& item
	);

	void AddBuyerDialogueScriptForItem(
		const CustomerTradeItemDef& item
	);

	std::vector<ItemPlacementSpot> itemPlacementSpots;

	void BuildItemPlacementSpots();

	int FindBestPlacementSpotForProp(
		int propIndex,
		Vector3 nearPosition,
		bool preferHomeSpot
	) const;

	bool PlaceScenePropAtSpot(
		int propIndex,
		int spotIndex,
		bool lockPhysics
	);

	void ClearScenePropPlacementSpot(int propIndex);

	bool TrySnapHeldPropToPlacementSpot(int propIndex);

	int targetedPlacementSpotIndex = -1;
	bool placementPreviewValid = false;
	int placementPreviewPropIndex = -1;
	Vector3 placementPreviewPosition{ 0.0f, 0.0f, 0.0f };
	Vector3 placementPreviewRotationDeg{ 0.0f, 0.0f, 0.0f };

	bool IsPlacementSpotValidForProp(
		int propIndex,
		int spotIndex
	) const;

	int FindBestPlacementSpotForPropFromRay(
		int propIndex,
		Ray ray,
		float maxDistance
	) const;

	void ComputePlacementPreviewForSpot(
		int propIndex,
		int spotIndex,
		Vector3& outPosition,
		Vector3& outRotationDeg
	) const;

	void UpdateHeldPlacementPreview();
	void DrawHeldPlacementPreview() const;

	float GetYawToTargetXZ(Vector3 from, Vector3 to) const;
	void SetCustomerYawTowards(int customerIndex, Vector3 target);
	void ApplyPOIFacingToCustomer(int customerIndex, int poiIndex);





	struct SceneProp
	{
		std::string name = "Prop";
		Model* model = nullptr;

		Vector3 position{ 0.0f, 0.0f, 0.0f };
		Vector3 rotationDeg{ 0.0f, 0.0f, 0.0f };
		Vector3 scale{ 1.0f, 1.0f, 1.0f };

		Color tint = WHITE;
		bool visible = true;
		bool lockUniformScale = false;

		bool hasCollision = true;
		bool blocksPlayer = true;
		bool useNormalCollision = false;
		bool useJoltCollider = true;
		bool simulatePhysics = false;
		bool syncFromPhysics = false;
		bool editLockPhysics = false;
		bool canPickup = false;

		// Item placement / shelf / counter spot support
		std::string itemTag = "generic";

		std::string inspectDialogueTag = "";
		std::vector<std::string> inspectDialogueLines;

		int currentPlacementSpotIndex = -1;
		int homePlacementSpotIndex = -1;

		bool preferHomePlacement = true;
		bool placedByCustomer = false;

		int owningCustomerIndex = -1;
		bool scannedForCustomer = false;
		int scannedPriceYen = 0;



		bool castsShadow = true;

		Vector3 holdOffsetLocal = { 0.0f, -0.2f, 0.0f };
		// x = right/left, y = up/down, z = extra forward/back from the hold point

		Vector3 holdRotationOffsetDeg = { 0.0f, 0.0f, 0.0f };
		// fixes imported model local rotation when held

		bool holdFollowCameraPitch = false;
		// false = stay upright, only face camera yaw
		// true  = rotate with full camera pitch + yaw

		bool snapUprightOnDrop = true;
		// x/z reset upright when released

		Vector3 dropRotationOffsetDeg = { 0.0f, 0.0f, 0.0f };
		// optional extra correction when dropped

		Vector3 colliderOffset{ 0.0f, 0.0f, 0.0f };
		Vector3 colliderSize{ 1.0f, 1.0f, 1.0f };

		TextureUsageFlags texUsage{ 1, 1, 1, 0 };

		std::vector<int> transparentMaterialIndices;
		unsigned char transparentAlpha = 255;

		std::string sourceGlbPath = "";
		std::string sourceNodeName = "";
		int sourceNodeIndex = -1;
		bool importedFromGlbScene = false;

		int parentIndex = -1;
		std::vector<int> childIndices;

		Vector3 localPosition{ 0.0f, 0.0f, 0.0f };
		Vector3 localRotationDeg{ 0.0f, 0.0f, 0.0f };
		Vector3 localScale{ 1.0f, 1.0f, 1.0f };

		Matrix worldMatrix = MatrixIdentity();

		std::vector<int> materialAlphaModes;       // 0 opaque, 1 mask, 2 blend
		std::vector<float> materialAlphaCutoffs;

		std::vector<float> materialReflectionStrengths;

		std::vector<int> meshAlphaModes;
		std::vector<float> meshAlphaCutoffs;
		std::vector<float> meshBaseAlphas;
		std::vector<float> meshReflectionStrengths;

		BoundingBox localRenderBounds{};
		Vector3 localRenderCenter{ 0.0f, 0.0f, 0.0f };
		float localRenderRadius = 1.0f;
		bool renderBoundsReady = false;

		Vector3 importBasePosition{ 0.0f, 0.0f, 0.0f };
		Vector3 importBaseRotationDeg{ 0.0f, 0.0f, 0.0f };
		Vector3 importBaseScale{ 1.0f, 1.0f, 1.0f };

		Vector3 importedEditorOffset{ 0.0f, 0.0f, 0.0f };
		Vector3 importedEditorRotationDeg{ 0.0f, 0.0f, 0.0f };
		Vector3 importedEditorScale{ 1.0f, 1.0f, 1.0f };


		bool canOcclude = false;
		bool ignoreOcclusionCulling = false;

		Matrix importedBindWorldMatrix = MatrixIdentity();
		bool importedBindWorldMatrixReady = false;
		bool manualColliderOverride = false;

		PickupItemCategory pickupCategory = PickupItemCategory::Generic;
		std::string tradeItemId = "";

		JPH::BodyID bodyId;
	};


	struct InstancedPropState
	{
		std::string name;
		int type = 0;

		Vector3 position{};
		Vector3 rotationDeg{};
		Vector3 scale{ 1.0f, 1.0f, 1.0f };

		bool visible = true;
	};

	struct ScenePropState
	{
		std::string name;
		std::string key;

		std::string sourceGlbPath = "";
		std::string sourceNodeName = "";
		int sourceNodeIndex = -1;
		bool importedFromGlbScene = false;

		Vector3 importedEditorOffset{ 0.0f, 0.0f, 0.0f };
		Vector3 importedEditorRotationDeg{ 0.0f, 0.0f, 0.0f };
		Vector3 importedEditorScale{ 1.0f, 1.0f, 1.0f };

		int parentIndex = -1;

		Vector3 localPosition{ 0, 0, 0 };
		Vector3 localRotationDeg{ 0, 0, 0 };
		Vector3 localScale{ 1, 1, 1 };

		Vector3 position{ 0, 0, 0 };
		Vector3 rotationDeg{ 0, 0, 0 };
		Vector3 scale{ 1, 1, 1 };

		bool lockUniformScale = true;

		bool visible = true;
		bool hasCollision = true;
		bool blocksPlayer = true;

		bool useNormalCollision = false;
		bool useJoltCollider = true;

		bool simulatePhysics = false;
		bool syncFromPhysics = false;
		bool editLockPhysics = false;

		bool castsShadow = true;
		Vector3 colliderOffset{ 0, 0, 0 };
		Vector3 colliderSize{ 1, 1, 1 };

		// optional newer fields if you want undo for these too
		Vector3 holdOffsetLocal{};
		Vector3 holdRotationOffsetDeg{};
		bool holdFollowCameraPitch = false;
		bool snapUprightOnDrop = true;
		Vector3 dropRotationOffsetDeg{};


		TextureUsageFlags texUsage{};
		std::vector<int> transparentMaterialIndices;
		unsigned char transparentAlpha = 255;

		std::string itemTag = "generic";

		PickupItemCategory pickupCategory = PickupItemCategory::Generic;
		std::string tradeItemId = "";

		std::string inspectDialogueTag = "";
		std::vector<std::string> inspectDialogueLines;

		int currentPlacementSpotIndex = -1;
		int homePlacementSpotIndex = -1;
		bool preferHomePlacement = true;
		bool placedByCustomer = false;
		bool manualColliderOverride = false;
	};

	struct CustomerState
	{
		int index = -1;

		Vector3 position{ 0, 0, 0 };
		Vector3 targetPosition{ 0, 0, 0 };
		float yawDeg = 0.0f;
		Vector3 scale{ 1, 1, 1 };

		float moveSpeed = 0.75f;

		bool usePOINavigation = true;
		bool editorFrozen = false;

		int currentPOIIndex = -1;
		int targetPOIIndex = -1;
		int destinationPOIIndex = -1;

		std::string dialogueScriptId = "default_customer";
		std::string poiGroup = "any";
	};

	Matrix BuildTRSMatrix(Vector3 pos, Vector3 rotDeg, Vector3 scale) const;
	void UpdateScenePropWorldTransforms();
	void UpdateScenePropWorldTransformRecursive(int propIndex, Matrix parentMatrix);
	void ApplyImportedCollisionNamingRules();
	bool IsImportedScenePropColliderUsable(const SceneProp& prop) const;

	enum class HeldItemScanState
	{
		None,
		MovingToScanner,
		HoldAtScanner,
		ReturningToHand
	};

	HeldItemScanState heldItemScanState = HeldItemScanState::None;

	int scanningScenePropIndex = -1;
	int scanningSpotIndex = -1;

	float scanTimer = 0.0f;
	float scanMoveDuration = 0.55f;
	float scanHoldDuration = 0.35f;

	Sound scanSound{};
	bool scanSoundLoaded = false;
	float scanSoundVolume = 0.85f;

	std::unordered_map<std::string, Sound> sfx;
	std::unordered_map<std::string, float> sfxBaseVolumes;

	void LoadSoundEffects();
	void UnloadSoundEffects();

	bool LoadSfx(
		const std::string& id,
		const std::string& path,
		float volume = 1.0f
	);

	void PlaySfx(
		const std::string& id,
		bool restartIfPlaying = true
	);

	std::string activeLoopingSfxId = "";
	bool loopingSfxActive = false;

	void PlayLoopingSfx(const std::string& id, bool restartIfDifferent = true);
	void StopLoopingSfx(const std::string& id = "");
	void UpdateLoopingSfx();

	bool ShouldInspectSfxLoopForTag(const std::string& tag) const;

	Vector3 scanStartPos{ 0.0f, 0.0f, 0.0f };
	Quaternion scanStartRot{ 0.0f, 0.0f, 0.0f, 1.0f };

	Vector3 scanTargetPos{ 0.0f, 0.0f, 0.0f };
	Quaternion scanTargetRot{ 0.0f, 0.0f, 0.0f, 1.0f };


	bool TryReturnHeldScannedCustomerItemToCounter();
	int FindBestCounterOfferReturnSpotForProp(int propIndex) const;
	void ClearHeldStateAfterCustomerTakesItem(int propIndex);

	int ResolveCustomerOwnerForCounterProp(int propIndex) const;

	bool IsScannerSpot(int spotIndex) const;
	bool StartHeldItemScan(int propIndex, int spotIndex);
	void UpdateHeldItemScan(float dt);
	void MarkHeldItemScanned(int propIndex);
	void ComputeHeldItemTargetTransform(Vector3& outPos, Quaternion& outRot) const;
	bool TryStartHeldItemScanFromCurrentTarget(int propIndex);
	void SyncScanningScenePropFromBody();

	void OnScenePropPlacedAtSpot(int propIndex, int spotIndex);
	void CompleteBuyerReturnAfterScan(int customerIndex);

	Vector3 GetScenePropBodyCenterForPose(
		const SceneProp& prop,
		Vector3 propPosition,
		Vector3 propRotationDeg
	) const;

	Vector3 GetScenePropRotationForPlacementSpot(
		const SceneProp& prop,
		const ItemPlacementSpot& spot
	) const;

	Vector3 GetScenePropPositionForPlacementSpot(
		const SceneProp& prop,
		const ItemPlacementSpot& spot,
		Vector3 finalRotationDeg
	) const;
	void AccumulateScenePropSubtreeBoundsLocal(
		int propIndex,
		Matrix rootInverse,
		BoundingBox& outBounds,
		bool& hasBounds
	) const;

	bool RebuildScenePropSubtreeCollider(int propIndex);
	void DrawScenePropColliderDebug() const;

	bool drawPhysicsDebugBounds = false;
	bool drawSelectedScenePropBounds = false;
	bool drawEditorPointDebug = true;
	bool drawTestDynamicBoxDebug = false;
	bool drawCustomerInteractBounds = false;


	enum class SceneItemType
	{
		None,
		FloorModel,
		PropModel,
		Light,
		BlockoutBox,
		PhysicsBox
	};

	enum class MainMenuSceneType
	{
		Showcase,
		SavedSceneProps,
		GachaDisplay
	};



	void UploadMainMenuLightingToShader(const Camera3D& menuCamera) const;

	enum class GameScreen
	{
		Loading,
		MainMenu,
		Controls,
		AudioSettings,
		GraphicsSettings,
		Credits,
		Playing,
		Paused
	};
	GameScreen currentScreen = GameScreen::MainMenu;
	GameScreen menuReturnScreen = GameScreen::MainMenu;

	bool devStartDirectlyInGame = GAME_ENABLE_EDITOR != 0;

	GameScreen screenAfterLoading = GameScreen::MainMenu;

	bool requestQuit = false;
	float creditsScrollY = 0.0f;


	void SetScreen(GameScreen screen);
	void UpdateGameplay(float dt);
	void StartNewGame();
	void ResumeGame();
	void ReturnToMainMenu();
	void DrawCreditsMenu();

	void DrawMainMenu();
	void DrawMainMenu3DBackground();
	MainMenuSceneType mainMenuSceneType = MainMenuSceneType::SavedSceneProps;

	void DrawMainMenuShowcaseScene(const Camera3D& menuCamera);
	void DrawMainMenuSavedSceneProps(const Camera3D& menuCamera);
	void DrawMainMenuGachaDisplayScene(const Camera3D& menuCamera);
	void PrepareMainMenuPBR(const Camera3D& menuCamera, int useAlbedo, int useNormal, int useMRA);

	void DrawControlsMenu();
	void DrawAudioSettingsMenu();
	void DrawPauseMenu();

	bool DrawUIButton(Rectangle rect, const char* text);
	bool DrawUISlider(Rectangle rect, const char* label, float& value);

	void OpenControlsMenu(GameScreen returnScreen);
	void OpenAudioSettingsMenu(GameScreen returnScreen);

	bool cursorCapturedByGame = false;
	bool hasInitializedScreen = false;
	bool IsMenuScreen(GameScreen screen) const;
	void DrawGraphicsSettingsMenu();
	void OpenGraphicsSettingsMenu(GameScreen returnScreen);

	int loadingStep = 0;
	bool loadingStepPrimed = false;
	float loadingProgress = 0.0f;
	double loadingStartTime = 0.0;
	std::string loadingStatus = "Starting...";

	int GetLoadingStepCount() const;
	const char* GetLoadingStepName(int step) const;
	void UpdateLoading();
	void DrawLoadingScreen();

	bool startNewGameRequested = false;	
	bool editMode = false;
	bool blockoutDirty = false;

	SceneItemType selectedSceneItemType = SceneItemType::None;
	int selectedSceneItemIndex = -1;

	int selectedCustomerIndex = -1;

	void DrawCustomerTreeNode(int customerIndex);
	void DrawSelectedCustomerInspectorPanel();
	void ApplyCustomerEditorTransform(int customerIndex);

	bool IsQueueCustomerState(CustomerAIState state) const;
	bool IsCounterCustomerState(CustomerAIState state) const;

	bool IsCustomerAtAssignedQueueSlot(int customerIndex) const;
	bool IsCustomerAtFrontQueueSlot(int customerIndex) const;

	void AssignCustomerToQueueSlot(
		int customerIndex,
		int queueSlotIndex,
		CustomerAIState movingState
	);

	void UpdateCustomerQueueState(int customerIndex);
	bool CanTalkToCustomerNow(
		int customerIndex,
		std::string* blockedReason
	) const;
	void AdvanceQueueLine();
	bool IsCustomerScannedItemReturnedToCounterOffer(int customerIndex) const;

	void CompleteCounterCustomerService(int customerIndex);
	bool IsCustomerCounterItemStillOnCounter(int customerIndex) const;
	void OnCustomerDialogueCompleted(int customerIndex, const std::string& scriptId);

	void DrawBottomPrompt(const char* text, int yOffset = 120) const;
	void DrawGameplayPrompts();

	void StartSelfInspectDialogueForHeldItem();
	bool IsSelfInspectDialogueActive() const;

	static std::string JoinLines(const std::vector<std::string>& lines);
	static std::vector<std::string> SplitLines(const std::string& text);

	Vector3 floorDrawPos = { 0.0f, 0.0f, 0.0f };
	Vector3 propDrawPos = { 0.0f, 0.5f, 4.0f };

	Vector3 floorDrawRotDeg = { 0.0f, 0.0f, 0.0f };
	Vector3 propDrawRotDeg = { 0.0f, 0.0f, 0.0f };

	bool ShouldPurgeRuntimeCustomerItemProp(const SceneProp& prop) const;
	void PurgeRuntimeCustomerItemSceneProps();

	void DrawSceneEditorUI();
	void RecreatePhysicsWorld();

	std::vector<BlockoutBox> blockoutBoxes;

	void BuildStoreBlockout();
	void DrawStoreBlockout() const;
	BoundingBox BoxToBounds(const BlockoutBox& box) const;

	//collision
	std::vector<PhysicsBody> staticBodies;
	std::vector<PhysicsBody> dynamicBodies;

	void BuildStaticBodiesFromBlockout();
	void DrawPhysicsDebug() const;

	AABB MakeAABB(const Vector3& position, const Vector3& halfExtents) const;
	AABB MakeBlockoutAABB(const BlockoutBox& box) const;

	void RepairScenePropHierarchyLinks();

	Matrix GetImportedEditorDrawMatrix(const SceneProp& prop) const;
	void ApplyImportedEditorTransformToRuntime(SceneProp& prop);
	void SyncImportedEditorOffsetFromRuntime(SceneProp& prop);
	BoundingBox GetPlayerBoundsAt(const Vector3& pos) const;
	bool Intersects(const BoundingBox& a, const BoundingBox& b) const;
	BoundingBox BodyToBounds(const PhysicsBody& body) const;
	void ResolvePlayerCollisions();


	std::unique_ptr<PhysicsWorld> physics;

	JPH::BodyID testDynamicBox;
	bool hasTestDynamicBox = false;

	std::vector<SceneProp> sceneProps;

	//holding
	bool isHoldingBox = false;
	float holdDistance = 0.5f;
	void HandleBoxInteraction();
	Vector3 GetCameraForward() const;

	bool ScenePropHasTransparentMeshes(const SceneProp& prop) const;
	void DrawScenePropByIndex(int propIndex) const;

	void DrawScenePropByIndexPass(int propIndex, bool transparentPass) const;

	void DrawScenePropShadow(const SceneProp& prop) const;
	PlacementHit FindPlacementSurfaceRaycast(Vector3 rayOrigin, Vector3 rayDir, float maxDistance, int ignoreScenePropIndex) const;
	PlacementHit FindDownwardPlacementRaycast(Vector3 worldPos, float probeHeight, int ignoreScenePropIndex) const;
	//cursor
	bool cursorUnlocked = false;

	void ToggleCursorMode();

	//edit mode
	bool editCamNavigating = false;

	Camera3D editorCamera{};
	Vector3 editorCamPos{ 0.0f, 2.0f, 0.0f };
	float editorYaw = 0.0f;
	float editorPitch = 0.0f;
	float editorMoveSpeed = 6.0f;
	float editorLookSpeed = 0.0035f;

	void EnterEditMode();
	void ExitEditMode();
	void UpdateEditCamera(float dt);
	void SyncEditorCameraFromCurrentCamera();

	bool SaveSceneState();
	bool LoadSceneState();
	Vector3 physicsBoxSavedPos = { 0.0f, 3.0f, 0.0f };
	Quaternion physicsBoxSavedRot = { 0.0f, 0.0f, 0.0f, 1.0f };

	struct CinematicTriggerZone
	{
		std::string id = "cinematic_trigger";

		Vector3 position{ 0.0f, 1.0f, 0.0f };
		Vector3 size{ 2.0f, 2.0f, 2.0f };

		Vector3 lookTarget{ 0.0f, 1.5f, 0.0f };

		float duration = 1.5f;

		bool enabled = true;
		bool repeatable = false;
		bool lockPlayerMovement = true;
		bool syncPlayerLookAtEnd = true;

		// New: optional self-dialogue played when this trigger fires.
		std::vector<std::string> selfDialogueLines;
		bool playSelfDialogue = false;

		// Optional voice prefix. If empty, trigger.id is used.
		std::string selfDialogueVoicePrefix = "";

		// Optional look-sound trigger.
// Example: cat purrs only while player is looking at this trigger box.
		bool enableLookSound = false;
		std::string lookSoundSfxId = "";
		float lookSoundDistance = 3.0f;
		bool lookSoundLoop = true;

		float lookSoundVolume = 0.55f;
		float lookSoundFadeInSeconds = 0.35f;
		float lookSoundFadeOutSeconds = 0.50f;
		// Runtime only. Do not save these.
		bool triggered = false;
		bool wasInside = false;
	};

	// edit - do undo
	struct SceneSnapshot
	{
		std::string floorName;
		std::string propName;
		std::string physicsBoxName;

		Vector3 floorPos{};
		Vector3 floorRotDeg{};
		Vector3 propPos{};
		Vector3 propRotDeg{};

		Vector3 physicsBoxPos{};
		Quaternion physicsBoxRot{};

		Color pickupOutlineColor = { 80, 220, 255, 255 };
		float pickupOutlineWidth = 4.0f;

		bool pickupOutlinePulseEnabled = true;
		float pickupOutlinePulseMinWidth = 2.0f;
		float pickupOutlinePulseMaxWidth = 4.0f;
		float pickupOutlinePulseSpeed = 3.0f;
		int pickupOutlinePulseMinAlpha = 140;
		int pickupOutlinePulseMaxAlpha = 255;

		bool pickupOccludedOutlineEnabled = true;
		float pickupOccludedOutlineWidth = 2.0f;
		Color pickupOccludedOutlineColor = { 80, 220, 255, 180 };

		bool pickupHiddenFillEnabled = true;
		float pickupHiddenFillWidth = 0.0f;
		Color pickupHiddenFillColor = { 80, 220, 255, 85 };

		std::vector<BlockoutBox> blockoutBoxes;
		std::vector<ScenePropState> sceneProps;
		std::vector<InstancedPropState> instancedProps;

		bool shadowMapsUpdateEveryFrame = false;
		bool shadowMapsUpdateWhenDynamicPropsExist = true;
		bool shadowMapsUpdateForCustomers = false;

		std::vector<CustomerPOI> customerPOIs;

		std::vector<CustomerState> customers;


		std::vector<ItemPlacementSpot> itemPlacementSpots;

		std::vector<CinematicTriggerZone> cinematicTriggers;
		int selectedCinematicTriggerIndex = -1;

		Light lights[MAX_LIGHTS];
		int lightCount = 0;
	};

	Model blockoutCubeModel{};

	std::vector<SceneSnapshot> undoStack;
	std::vector<SceneSnapshot> redoStack;

	SceneSnapshot CaptureSceneSnapshot() const;
	void ApplySceneSnapshot(const SceneSnapshot& snapshot);
	void PushUndoSnapshot();
	void HandleUndoRedo();
	void PushUndoIfItemActivated();

	std::string GetScenePropStateKey(const SceneProp& prop, int index) const;

	std::vector<ScenePropState> CaptureScenePropStates() const;
	void ApplyScenePropStates(const std::vector<ScenePropState>& states);
	void RestoreCorruptedImportedScenePropNames();

	void SyncScenePropsFromPhysics();

	std::vector<CustomerState> CaptureCustomerStates() const;
	void ApplyCustomerStates(const std::vector<CustomerState>& states);
	bool IsCustomerInActiveDialogue(int customerIndex) const;
	void ApplyAllScenePropTransformsToBodies();

	//Jolt player colliders
	JPH::Ref<JPH::CharacterVirtual> playerCharacter;
	bool playerUsesJolt = true;

	float playerRadius = 0.35f;
	float playerHeight = 1.8f;

	void CreateVirtualPlayer();
	void UpdateVirtualPlayer(float dt);
	void SyncPlayerFromVirtual();
	void SetVirtualPlayerFootPosition(Vector3 footPosition);
	void ResolvePlayerCustomerSlide();
	bool PlayerWouldOverlapCustomer(Vector3 playerPosition) const;
	void MoveVirtualPlayerToPosition(Vector3 playerPosition);

	// outline
	Shader outlineShader{};

	int outlineWidthLoc = -1;
	int outlineColorLoc = -1;
	int outlineViewLoc = -1;
	int outlineProjectionLoc = -1;

	int targetedScenePropIndex = -1;

	Matrix GetScenePropPreviewDrawMatrix(
		const SceneProp& prop,
		Vector3 previewPosition,
		Vector3 previewRotationDeg
	) const;

	void DrawScenePropOutlineWithMatrix(
		int propIndex,
		const Matrix& transform,
		const Camera3D& cam,
		bool redrawActualSceneProp
	) const;

	// visible outline
	Color pickupOutlineColor = { 80, 220, 255, 255 };
	float pickupOutlineWidth = 4.0f;

	void PreparePickupScenePropsForPhysics();

	// pulse only affects the visible outline
	bool pickupOutlinePulseEnabled = true;
	float pickupOutlinePulseMinWidth = 2.0f;
	float pickupOutlinePulseMaxWidth = 4.0f;
	float pickupOutlinePulseSpeed = 3.0f;
	int pickupOutlinePulseMinAlpha = 140;
	int pickupOutlinePulseMaxAlpha = 255;

	// occluded outline (draw through table / shelf / walls)
	bool pickupOccludedOutlineEnabled = true;
	float pickupOccludedOutlineWidth = 2.0f;
	Color pickupOccludedOutlineColor = { 80, 220, 255, 180 };

	// hidden fill through objects
	bool pickupHiddenFillEnabled = true;
	float pickupHiddenFillWidth = 0.0f;
	Color pickupHiddenFillColor = { 80, 220, 255, 85 };

	void DrawOutlineSettingsUI();
	float GetPickupOutlinePulse01() const;
	float GetCurrentVisibleOutlineWidth() const;
	Color GetCurrentVisibleOutlineColor() const;


	struct OutlineModelCache
	{
		const Model* source = nullptr;
		std::vector<Mesh> meshes;
	};

	std::vector<OutlineModelCache> outlineModelCaches;

	void BuildOutlineModelCache(const Model* model);
	const OutlineModelCache* GetOutlineModelCache(const Model* model) const;
	void UnloadOutlineModelCaches();

	void UpdateInteractableTarget();
	void DrawScenePropOutlineByIndex(int propIndex, const Camera3D& cam) const;
	void DrawInteractableOutline(const Camera3D& cam) const;

	std::string floorName = "Floor";
	std::string propName = "Prop";
	std::string physicsBoxName = "Physics Box";

	std::vector<EditorItem> editorItems;
	int selectedEditorItemIndex = -1;

	void RebuildEditorItems();
	void DrawEditorItemList();
	void DrawEditorItemInspector(EditorItem& item);

	int selectedScenePropIndex = -1;

	void DrawScenePropHierarchyPanel();
	void DrawScenePropTreeNode(int propIndex);
	void DrawSelectedScenePropInspectorPanel();

	void SyncScenePropLocalFromWorld(SceneProp& prop);

	void ApplyImportedEditorTransformsToRuntime();

	void AddSceneProp(
		const std::string& name,
		Model* model,
		Vector3 position,
		Vector3 rotationDeg,
		Vector3 scale,
		bool hasCollision,
		Vector3 colliderSize,
		Vector3 colliderOffset = { 0, 0, 0 },
		bool useJoltCollider = true,
		bool useNormalCollision = false,
		Color tint = WHITE
	);

	int FindItemPlacementSpotById(const std::string& id) const;

	void RebuildItemPlacementSpotOccupancy();

	int FindFirstFreePlacementSpot(
		ItemPlacementSpotKind kind,
		bool forCustomer
	) const;



	int CreateCounterOfferItemForCustomer(int customerIndex);

	void DrawItemPlacementSpotHierarchySection();
	void DrawSelectedItemPlacementSpotInspectorPanel();

	int selectedItemPlacementSpotIndex = -1;


	bool placementUndoPending = false;
	void BuildSceneProps();
	void DrawSceneProps(const Camera3D& cam);
	Matrix GetScenePropDrawMatrix(const SceneProp& prop) const;

	void DisableShadowsForShader(Shader shader) const;

	bool GetScenePropRenderBoundsWorld(
		const SceneProp& prop,
		BoundingBox& outBounds
	) const;
	void DrawScenePropRenderBoundsDebug() const;
	void BuildStaticBodiesFromSceneProps();

	void CaptureLiveScenePropPhysicsTransforms();

	void RefreshScenePropRenderBounds(SceneProp& prop);
	void RefreshAllScenePropRenderBounds();


	bool GetScenePropCullSphereWorld(
		const SceneProp& prop,
		Vector3& outCenter,
		float& outRadius
	) const;

	bool ScenePropNeedsTransparentPass(const SceneProp& prop) const;
	Matrix GetScenePropColliderBaseMatrix(const SceneProp& prop) const;
	bool GetScenePropModelBoundsInColliderLocal(
		const SceneProp& prop,
		BoundingBox& outBounds
	) const;

	std::vector<int> visibleScenePropIndices;
	Vector3 GetScaledColliderOffset(const SceneProp& prop) const;
	void ResetScenePropColliderToModelBounds(SceneProp& prop);
	Vector3 GetScenePropRotatedOffset(const SceneProp& prop) const;
	void ApplyScenePropTransformToBody(SceneProp& prop);
	void ReadScenePropTransformFromBody(SceneProp& prop);

	struct ScreenOccluder
	{
		int propIndex = -1;
		Rectangle rect{};
		float depth = 0.0f;
		BoundingBox worldBounds{};
	};


	bool enableGpuOcclusionCulling = false;


	std::vector<unsigned int> scenePropOcclusionQueries;
	std::vector<unsigned char> scenePropOcclusionVisible;
	std::vector<unsigned char> scenePropOcclusionPending;

	int gpuOcclusionQueriesIssued = 0;
	int gpuOcclusionCulled = 0;

	int debugGpuOccRejectNoQueryArray = 0;
	int debugGpuOccRejectShouldTest = 0;
	int debugGpuOccRejectPending = 0;
	int debugGpuOccRejectNoBounds = 0;

	int maxGpuOcclusionQueriesPerFrame = 8;
	float minGpuOcclusionQueryScreenArea = 2500.0f;
	int gpuOcclusionCursor = 0;

	std::vector<unsigned char> scenePropWasFrustumVisible;
	std::vector<unsigned char> scenePropOcclusionGraceFrames;

	int gpuOcclusionRevealGraceFrames = 1;

	bool ShouldGpuOcclusionQueryByScreenSize(
		int propIndex,
		const Camera3D& cam
	) const;

	void EnsureScenePropOcclusionQueries();
	void ReadScenePropOcclusionQueryResults();
	bool ShouldGpuOcclusionTestSceneProp(int propIndex) const;
	void IssueScenePropOcclusionQuery(int propIndex);
	void UnloadScenePropOcclusionQueries();

	std::vector<int> frustumVisibleScenePropIndices;
	std::vector<ScreenOccluder> screenOccluders;

	bool enableOcclusionCulling = false;
	float occlusionCoverageThreshold = 0.92f;
	float occlusionDepthBias = 0.35f;
	float minOccluderScreenArea = 500.0f;


	int debugOccluderRejectedNoModel = 0;
	int debugOccluderRejectedNotMarked = 0;
	int debugOccluderRejectedTransparent = 0;
	int debugOccluderRejectedNoRect = 0;
	int debugOccluderRejectedSmall = 0;

	bool GetScenePropScreenRect(
		int propIndex,
		const Camera3D& cam,
		Rectangle& outRect,
		float& outDepth
	) const;

	void BuildScenePropOccluders(
		const Camera3D& cam,
		const std::vector<int>& candidateIndices
	);

	bool IsScenePropOccludedByScreenOccluders(
		int propIndex,
		const Camera3D& cam
	) const;

	JPH::BodyID heldBody;
	bool hasHeldBody = false;
	float interactDistance = 1.5f;
	int heldScenePropIndex = -1;
	int FindScenePropIndexByBody(JPH::BodyID id) const;

	struct ImportedGlbMasterModel
	{
		std::string path;
		Model model{};
	};

	std::vector<std::unique_ptr<ImportedGlbMasterModel>> importedGlbMasters;
	std::vector<std::unique_ptr<Model>> importedGlbModelViews;

	void ClearImportedGlbScene();
	Model* CreateImportedModelView(Model& master, int firstMesh, int meshCount);
	void ImportGlbSceneAsProps(const char* glbPath);

	struct InteractHit
	{
		JPH::BodyID bodyId;
		float distance = 99999.0f;
		bool valid = false;

		int scenePropIndex = -1;
	};

	InteractHit FindInteractableBodyRaycast() const;
	bool RaycastAgainstOBB(Vector3 rayOrigin, Vector3 rayDir,
		Vector3 boxCenter, Quaternion boxRotation,
		Vector3 boxSize, float maxDistance,
		float& outDistance) const;

	mutable std::vector<int> interactableScenePropIndices;
	mutable int interactableScenePropCacheSceneCount = -1;
	mutable bool interactableScenePropCacheDirty = true;

	void MarkInteractableScenePropCacheDirty();
	void RebuildInteractableScenePropIndexCache() const;

	void ClearHeldPropTarget();
	void StartHoldingBody(JPH::BodyID id);
	void StopHoldingBody();
	void UpdateHeldBody();
	void ApplyScenePropPhysicsModesAfterRebuild();
	bool pendingPostLoadPhysicsRestore = false;

	//skybox
	Model skyboxModel{};
	Shader skyboxShader{};
	Shader cubemapShader{};
	bool skyboxLoaded = false;

	void LoadSkybox(const char* fileName, bool useHDR = true);
	void DrawSkybox() const;
	void UnloadSkybox();

	// hold item
	float heldYawOffsetDeg = 180.0f;
	float heldPitchOffsetDeg = 0.0f;
	float heldRollOffsetDeg = 0.0f;

	bool inspectMode = false;
	bool inspectRestoreCursorLocked = false;

	float inspectDistance = 0.65f;
	float inspectMinDistance = 0.20f;
	float inspectMaxDistance = 1.0f;
	float inspectZoomStep = 0.08f;

	float inspectYawDeg = 0.0f;
	float inspectPitchDeg = 0.0f;
	float inspectRotateSpeed = 0.25f;

	bool inspectSmoothingInitialized = false;
	Vector3 inspectSmoothedPos{ 0.0f, 0.0f, 0.0f };
	Quaternion inspectSmoothedRot{ 0.0f, 0.0f, 0.0f, 1.0f };

	float inspectPosFollowSharpness = 14.0f;
	float inspectRotFollowSharpness = 14.0f;
	Vector3 inspectHoldOffsetLocal = { 0.0f, -0.02f, 0.35f };

	Vector3 worldAmbientColorCached = { 26.0f / 255.0f, 32.0f / 255.0f, 135.0f / 255.0f };
	float worldAmbientIntensityCached = 0.08f;

	Vector3 inspectAmbientColor = { 0.20f, 0.20f, 0.20f };
	float inspectAmbientIntensity = 0.24f;

	float inspectLightIntensity = 5.f;
	float inspectLightRightOffset = 0.28f;   // more side lighting
	float inspectLightUpOffset = 0.18f;      // higher light
	float inspectLightForwardOffset = 0.14f; // less flat/front-on

	// Music box inspect easter egg

	bool musicBoxPausedRecordPlayer = false;
	bool musicBoxSavedRecordPlayerPaused = false;

	void PauseRecordPlayerForMusicBoxInspect();
	void ResumeRecordPlayerAfterMusicBoxInspect();

	float musicBoxSceneBlackoutAmount = 0.0f;
	float musicBoxSceneBlackoutFadeInSeconds = 0.10f;
	float musicBoxSceneBlackoutFadeOutSeconds = 1.25f;

	float GetMusicBoxSceneBlackoutAmount() const;

	bool musicBoxInspectActive = false;
	float musicBoxInspectTimer = 0.0f;
	float musicBoxInspectLightMultiplier = 1.0f;
	float musicBoxInspectAmbientMultiplier = 1.0f;

	bool musicBoxInspectBlackoutScene = true;

	void BeginMusicBoxInspectEasterEgg();
	void EndMusicBoxInspectEasterEgg();
	void UpdateMusicBoxInspectEasterEgg(float dt);

	bool IsHeldInspectItemTag(const std::string& tag) const;
	float GetMusicBoxInspectFlicker01() const;

	RenderTexture2D worldTarget{};
	RenderTexture2D blurPingTarget{};
	RenderTexture2D blurPongTarget{};

	Shader blurShader{};
	int blurDirectionLoc = -1;
	int blurTexelSizeLoc = -1;

	int inspectBlurPasses = 2;

	bool inspectJustEntered = false;
	void RecreatePostProcessTargets();
	void DrawWorldToTarget(const Camera3D& cam);
	void DrawBlurredWorldToScreen();

	void EnterInspectMode();
	void ExitInspectMode();
	void UpdateInspectMode(float dt);

	void UploadWorldLightingToShader() const;
	void UploadInspectionLightingToShader(const Camera3D& cam) const;


	struct LightUniformLocations
	{
		int numOfLights = -1;
		int viewPos = -1;
		int ambientColor = -1;
		int ambient = -1;

		int enabled[MAX_LIGHTS]{};
		int type[MAX_LIGHTS]{};
		int position[MAX_LIGHTS]{};
		int target[MAX_LIGHTS]{};
		int color[MAX_LIGHTS]{};
		int intensity[MAX_LIGHTS]{};
		int range[MAX_LIGHTS]{};
	};

	LightUniformLocations pbrLightLocs;
	LightUniformLocations animatedLightLocs;
	LightUniformLocations instancedLightLocs;

	int pbrAmbientColorLoc = -1;
	int pbrAmbientLoc = -1;

	void CacheLightUniformLocations(
		Shader shader,
		LightUniformLocations& locs
	);

	void UploadBlackoutLightingToShader(
		Shader shader,
		const LightUniformLocations& locs,
		Vector3 cameraPosition
	) const;

	bool ShouldMusicBoxBlackoutScene() const;


	RenderTexture2D heldPropTarget{};
	void RecreateHeldPropTarget();
	void DrawHeldPropToTarget(const Camera3D& cam);

	RenderTexture2D bloomExtractTarget = {};
	RenderTexture2D bloomPingTarget = {};
	RenderTexture2D bloomPongTarget = {};

	RenderTexture2D* RenderBloomToTarget();

	Shader brightExtractShader = {};
	int brightThresholdLoc = -1;
	int brightKneeLoc = -1;

	bool bloomEnabled = false;
	float bloomThreshold = 0.72f;
	float bloomKnee = 0.18f;
	float bloomIntensity = 0.65f;
	int bloomBlurPasses = 4;

	//model Gacha
	Model Gacha{};

	struct BallLocalTransform
	{
		std::string name;
		Vector3 position{};
		Quaternion rotation{};
		Vector3 scale{ 1.0f, 1.0f, 1.0f };
		Matrix localMatrix = MatrixIdentity();
	};

	enum class InstancePropType
	{
		GachaMachine = 0,
		Basket = 1
	};



	struct InstancedProp
	{
		std::string name;
		InstancePropType type = InstancePropType::GachaMachine;

		Vector3 position{};
		Vector3 rotationDeg{};
		Vector3 scale{ 1.0f, 1.0f, 1.0f };

		bool visible = true;
		bool hasCollision = true;
		bool blocksPlayer = true;
		bool autoCollider = true;

		Vector3 colliderOffset{ 0.0f, 0.0f, 0.0f };
		Vector3 colliderSize{ 1.0f, 1.0f, 1.0f };

		JPH::BodyID bodyId{};

	};

	
	std::vector<InstancedProp> instancedProps;
	int selectedInstancedPropIndex = -1;
	bool instancedPropsDirty = false;

	void CreateDefaultGachaInstanceProps();
	void RebuildInstancedPropTransforms();

	void DrawInstancedPropHierarchySection();
	void DrawSelectedInstancedPropInspectorPanel();
	std::vector<InstancedPropState> CaptureInstancedPropStates() const;
	void ApplyInstancedPropStates(const std::vector<InstancedPropState>& states);

	Vector3 gachaInstanceColliderSize{ 1.0f, 1.0f, 1.0f };
	Vector3 gachaInstanceColliderOffset{ 0.0f, 0.0f, 0.0f };
	bool gachaInstanceColliderReady = false;

	void ComputeGachaInstanceAutoCollider();
	void BuildStaticBodiesFromInstancedProps();
	void RebuildInstancedPropCollider(int index);
	void RemoveInstancedPropCollider(int index);

	bool CustomerWouldHitInstancedProps(Vector3 position) const;

	void DrawCustomerAIDebug() const;

	std::vector<BallLocalTransform> gachaBallLocalTransforms;
	InstancedModelBatch gachaBallInstanceBatch;
	Shader gachaBallInstancingShader{};

	bool LoadGachaBallLocalTransforms(const char* path);
	void LoadGachaBallInstanceTest();
	void BuildGachaBallWorldTransforms();
	void UnloadGachaBallInstances();


	std::vector<BallLocalTransform> basketCartridgeLocalTransforms;

	InstancedModelBatch basketInstanceBatch;
	InstancedModelBatch basketCartridgeInstanceBatch;

	mutable std::vector<Matrix> visibleBasketTransformsScratch;
	mutable std::vector<Matrix> visibleBasketCartridgeTransformsScratch;

	void BuildVisibleBasketInstanceTransforms(const Camera3D& cam) const;

	Vector3 basketInstanceColliderSize{ 1.0f, 1.0f, 1.0f };
	Vector3 basketInstanceColliderOffset{ 0.0f, 0.0f, 0.0f };
	bool basketInstanceColliderReady = false;

	void LoadBasketInstanceTest();
	void LoadBasketCartridgeInstanceTest();

	bool LoadBasketCartridgeLocalTransforms(const char* path);
	void BuildBasketCartridgeWorldTransforms();

	void DrawBasketInstances(const Camera3D& cam) const;
	void DrawBasketCartridgeInstances(const Camera3D& cam) const;

	void ComputeBasketInstanceAutoCollider();
	void UnloadBasketInstances();

	struct ScenePropPickResult
	{
		int index = -1;
		RayCollision collision{};
		bool hit = false;
	};

	bool PickScenePropAtMouse(
		const Camera3D& cam,
		ScenePropPickResult& outResult
	) const;

	void UpdateEditorScenePicking(const Camera3D& cam);

	bool ScenePropSubtreeContainsIndex(int propIndex, int targetIndex) const;

	bool sceneHierarchyScrollToSelected = false;

	//Customer
	Model customerModel{};
	std::vector<std::unique_ptr<Model>> customerModels;
	CustomerAnimSet customerAnimSet{};
	std::vector<Customer> customers;

	struct CustomerModelPoolItem
	{
		std::string typeId;
		std::unique_ptr<Model> model;
		bool inUse = false;
	};

	std::unordered_map<std::string, std::vector<CustomerModelPoolItem>> customerModelPools;

	void PreloadCustomerModelPool();
	void PreloadCustomerModelsForType(const std::string& typeId, int count);
	Model* AcquireCustomerModelFromPool(const std::string& typeId);
	void ReleaseCustomerModelToPool(Model* model);
	void ReleaseAllCustomerModelsToPool();
	void UnloadCustomerModelPool();
	int CountActiveCustomersOfType(const std::string& typeId) const;
	int CountActiveCustomersByRole(CustomerRole role) const;

	bool FindCustomerSpawnPositionNearPOI(int poiIndex, Vector3& outPosition) const;

	std::vector<JPH::BodyID> customerBodyIds;

	void BuildCustomerBodies();
	void AddCustomerBodyForCustomer(int customerIndex);
	void SyncCustomerBodiesToCustomers(float dt);
	void RemoveCustomerBodies();
	bool IsLargeStaticShellPropName(const SceneProp& prop) const;
	void ProcessPendingCustomerBodyCreates();

	void FreezeActiveDialogueCustomer();

	struct CustomerType
	{
		std::string id;
		std::string modelPath;
		std::string dialogueScriptId;

		std::string poiGroup = "any";

		CustomerRole role = CustomerRole::Browser;

		CustomerAnimSet animSet;

		Vector3 renderScale{ 0.01f, 0.01f, 0.01f };
		Vector3 renderRotationAxis{ 1.0f, 0.0f, 0.0f };
		float renderRotationAngleDeg = 90.0f;

		float moveSpeed = 0.6f;

		float pbrMetallicValue = 0.0f;
		float pbrRoughnessValue = 0.65f;
		float pbrRoughnessScale = 1.0f;
		float pbrReflectionStrength = 0.0f;
		float pbrEmissivePower = 3.0f;



	};
	std::unordered_map<std::string, CustomerType> customerTypes;

	Shader shadowDepthAnimatedShader  {};

	int animatedBoneMatricesLoc = -1;
	int shadowAnimatedBoneMatricesLoc = -1;

	void BuildCustomerTypes();
	void ApplyAnimatedCustomerMaterial(Model& model);
	void BuildCustomers();
	void UpdateCustomers(float dt);
	void DrawCustomers() const;
	void PrepareCustomerPBRMaterial() const;

	void DrawCustomerPBR(const Customer& customer) const;
	void ApplyAnimatedMaterialUniforms(const Material& mat) const;

	Customer* SpawnCustomerOfType(
		const std::string& typeId,
		Vector3 position,
		CustomerAnimState startState
	);

	int FindAnimationIndexByName(CustomerAnimSet& set, const char* name)
	{
		if (set.animations == nullptr) return -1;

		for (int i = 0; i < set.animationCount; i++)
		{
			if (strcmp(set.animations[i].name, name) == 0)
			{
				return i;
			}
		}

		return -1;
	}



	float GetFlatDistanceXZ(Vector3 a, Vector3 b) const;

	bool IsCustomerPositionBlocked(int customerIndex, Vector3 position) const;

	bool CustomerWouldHitSceneProps(Vector3 position) const;
	bool CustomerWouldHitBlockout(Vector3 position) const;
	bool CustomerWouldHitPlayer(Vector3 position) const;
	bool CustomerWouldHitOtherCustomers(int customerIndex, Vector3 position) const;

	void UpdateCustomersWithCollision(float dt);

	//animated shader
	Shader animatedPbrShader;
	void PrepareAnimatedCustomerPBRMaterial() const;

	void UploadLightsToShader(Shader shader) const;
	bool ShouldScenePropCastShadow(const SceneProp& prop, const Light& light) const;

	static constexpr int MAX_SHADOW_CASTERS = 2;

	struct ShadowCaster
	{
		bool active = false;
		int lightIndex = -1;

		RenderTexture2D shadowMap{};
		Camera3D camera{};

		Matrix lightVP = MatrixIdentity();

		float bias = 0.003f;
		float strength = 0.55f;
	};

	struct ShadowUniformLocations
	{
		int shadowCasterCount = -1;
		int shadowLightVP0 = -1;
		int shadowLightIndex0 = -1;
		int shadowBias0 = -1;
		int shadowStrength0 = -1;
		int shadowMap0 = -1;
	};

	ShadowUniformLocations pbrShadowLocs;
	ShadowUniformLocations animatedShadowLocs;
	ShadowUniformLocations instancedShadowLocs;

	void CacheShadowUniformLocations(
		Shader shader,
		ShadowUniformLocations& locs
	);

	ShadowCaster shadowCasters[MAX_SHADOW_CASTERS];


#ifdef __EMSCRIPTEN__
	int shadowMapSize = 1024;
#else
	int shadowMapSize = 4096;

#endif
	Shader shadowDepthShader{};

	void UpdateShadowCasters();
	void RenderShadowMaps();
	void DrawShadowCasters();
	void UploadShadowUniformsToShader(Shader shader) const;
	void AttachShadowTextureToSceneMaterials();
	void MarkShadowTextureBindingsDirty(bool shadowTextureRecreated = false);

	unsigned int lastAttachedShadowTextureId = 0;
	bool shadowTextureBindingsDirty = true;

	bool shadowMapsDirty = true;

	bool staticShadowMapDirty = true;
	bool dynamicShadowMapDirty = true;

	bool updateDynamicShadowMap = true;

	// Debug counters
	int shadowStaticPropsDrawn = 0;
	int shadowDynamicCustomersDrawn = 0;
	int shadowDynamicPropsDrawn = 0;



	bool ScenePropUsesDynamicShadowPass(int propIndex, const SceneProp& prop) const;

	void DrawInstancedBatchShadow(InstancedModelBatch& batch);
	void DrawStaticShadowCasters();
	void DrawDynamicShadowCasters();

	void RenderStaticShadowCache();
	void CopyStaticShadowCacheToFinalShadowMap();
	void RenderDynamicShadowsOntoFinalShadowMap();

	// Use this only while debugging or if you want live shadows no matter what.
	bool shadowMapsUpdateEveryFrame = false;

	// If a physics prop is moving/held, shadows may need to update every frame.
	bool shadowMapsUpdateWhenDynamicPropsExist = true;

	// If customers cast shadows and their animation/movement matters,
	// enable this. Otherwise keep false for better performance.
	bool shadowMapsUpdateForCustomers = true;

	void MarkShadowMapsDirty();
	bool HasDynamicShadowCasters() const;
	void RenderShadowMapsIfNeeded();
	void DrawShadowSettingsUI();

	// Dialogue
	RayDialNode* customerDialogueRoot = nullptr;
	RayDialManager* customerDialogueManager = nullptr;
	RayDialComponent* customerDialoguePanel = nullptr;
	RayDialComponent* customerDialogueLabel = nullptr;

	void RefreshDialogueUILayout();

	bool dialogueActive = false;
	int dialogueCustomerIndex = -1;
	int dialogueLineIndex = 0;


	bool customerDialogueAutoPlayEnabled = false;

	bool currentDialogueAutoPlayEnabled = false;
	bool currentDialogueHasValidVoice = false;
	bool dialogueVoiceFinished = false;

	float dialogueAutoAdvanceTimer = 0.0f;
	float dialogueAutoVoicePostDelay = 1.0f;
	float dialogueAutoTextPostDelay = 2.0f;

	bool dialogueFallbackBeepActive = false;

	bool IsCurrentDialogueSelfDialogue() const;
	bool IsCurrentDialogueCustomerDialogue() const;
	bool IsCurrentDialogueAutoPlayEnabled() const;

	bool AdvanceDialogueFromCurrentNode();
	void UpdateDialogueAutoPlay(float dt);

	bool PlayDialogueVoiceIfAvailable(const std::string& voicePath);
	void StartDialogueFallbackBeepIfNeeded();
	void StopDialogueFallbackBeep();
	void UpdateDialogueFallbackBeep();

	float customerTalkDistance = 2.0f;

	struct DialogueChoice
	{
		std::string text;
		int nextNode = -1;
	};

	enum class DialogueAction
	{
		None,
		PlaceCounterItem,

		BuyerPurchaseAccepted,
		BuyerPurchaseDeclined,

		SellerPurchaseAccepted,
		SellerPurchaseDeclined
	};


	struct DialogueNode
	{
		std::string speaker;
		std::string text;

		int nextNode = -1;
		bool endDialogue = false;

		std::vector<DialogueChoice> choices;

		std::string voicePath = "";

		CustomerAnimState customerAnim = CustomerAnimState::Idle;
		bool applyCustomerAnim = false;

		std::string nextScriptOnEnd = "";

		DialogueAction action = DialogueAction::None;

		// Optional: allows dialogue/self-dialogue nodes to trigger SFX too.
		std::string sfxId = "";
	};

	struct SelfDialogueLine
	{
		std::string text;
		std::string voicePath = "";
		std::string sfxId = "";
	};

	struct DialogueScript
	{
		std::string id;
		std::vector<DialogueNode> nodes;
	};

	std::unordered_map<std::string, DialogueScript> dialogueScripts;

	std::string activeDialogueScriptId = "";
	int activeDialogueCustomerIndex = -1;
	int currentDialogueNode = 0;
	int selectedDialogueChoice = 0;

	std::string fullDialogueText;
	std::string visibleDialogueText;

	double dialogueTextStartTime = 0.0;
	float dialogueCharsPerSecond = 35.0f;
	bool dialogueTextComplete = true;

	void StartDialogueTextAnimation(const std::string& text, float charsPerSecond = 35.0f);
	void UpdateDialogueTextAnimation();
	void CompleteDialogueTextAnimation();
	void SetDialogueLabelText(const std::string& text);
	void CompleteDialogueAndClose(const DialogueNode& endingNode);
	std::string currentDialogueText;

	Sound currentDialogueVoice{};
	bool hasDialogueVoice = false;
	DialogueScript* GetActiveDialogueScript();
	DialogueNode* GetActiveDialogueNode();


	std::string VoicePath(
		const std::string& scriptId,
		int nodeIndex
	) const;

	std::string SharedVoicePath(
		const std::string& fileBaseName
	) const;

	void RunDialogueAction(const DialogueNode& node);

	void CloseDialogue();

	void BuildCustomerDialogueTree();
	void SetDialogueNode(int nodeIndex);

	void BuildDialogue();
	void UpdateDialogue();
	void DrawDialogue();
	int FindTalkableCustomer();

	std::string blockedInteractionPrompt = "";

	int inspectTransitionFrames = 0;
	enum class RenderResolutionMode
	{
		Native = 0,      // Same as screen size. Best quality.
		Scale75 = 1,     // 75% of screen size.
		Scale50 = 2,     // 50% of screen size.
		P1440 = 3,       // 2560x1440 max.
		P1080 = 4,       // 1920x1080 max.
		P720 = 5         // 1280x720 max.
	};

	RenderResolutionMode renderResolutionMode = RenderResolutionMode::Native;

	// Actual allocated render target size.
	int renderTargetWidth = 0;
	int renderTargetHeight = 0;

	// Bloom remains cheaper than world render.
	int bloomDownscale = 2;

	// UI helper.
	const char* GetRenderResolutionModeName(RenderResolutionMode mode) const;
	void GetDesiredRenderTargetSize(int& outW, int& outH) const;
	void SetRenderResolutionMode(RenderResolutionMode mode);

	bool EnsureRenderTargetsMatchWindow();
	void ClearRenderTargetsOnce();

	//customer navigation


	std::vector<CustomerPOI> customerPOIs;
	int selectedCustomerPOI = -1;

	bool customerRoamingEnabled = true;

	void StopAllCustomerRoaming();

	void DrawCustomerPOIEditorUI();
	void DrawCustomerPOIDebug() const;
	void DrawItemPlacementSpotDebug() const;
	int CountCustomersUsingPOI(int poiIndex, int ignoreCustomerIndex = -1) const;
	bool IsPOIAvailableForCustomer(int customerIndex, int poiIndex) const;
	float GetRandomPOIWaitSeconds(const CustomerPOI& poi) const;
	Vector3 FindApproachPositionNearPOI(int customerIndex, int poiIndex);

	void RefreshLightShaderLocations(int index);
	int AddLightToScene(const Light& lightTemplate);
	int AddDefaultEditorLight();
	int DuplicateLight(int sourceIndex);
	int GetSelectedLightIndex() const;
	int FindEditorItemIndexForLight(int lightIndex) const;

	void DrawLightsEditorUI();
	void DrawLightInspector(int lightIndex);

	void SaveCustomerPOIs();
	void LoadCustomerPOIs();

	bool CanCustomerUsePOI(const Customer& customer, const CustomerPOI& poi, bool destinationOnly) const;
	bool IsCustomerPathBlocked(Vector3 from, Vector3 to) const;

	int PickNextCustomerDestinationPOI(const Customer& customer) const;
	int FindRandomPOIByKindForCustomer(
		CustomerPOIKind kind,
		const Customer& customer
	) const;

	int FindDetourPOI(const Customer& customer, Vector3 from, Vector3 destination, int destinationIndex) const;

	void UpdateCustomerPOINavigation(float dt);

	struct CustomerNavGrid
	{
		float minX = -10.0f;
		float maxX = 10.0f;
		float minZ = -10.0f;
		float maxZ = 10.0f;

		float cellSize = 0.45f;
		float agentRadius = 0.35f;

		int width = 0;
		int height = 0;

		std::vector<unsigned char> blocked;
		bool valid = false;
	};

	CustomerNavGrid customerNavGrid;
	bool customerNavGridDirty = true;
	bool drawCustomerNavGridDebug = false;

	float customerRepathInterval = 0.75f;

	int customerRoutesBuiltThisFrame = 0;
	int maxCustomerRoutesBuiltPerFrame = 1;

	void RebuildCustomerNavGrid();
	bool IsCustomerStaticBlockedAt(Vector3 position) const;
	bool IsCustomerDynamicBlockedAt(int selfCustomerIndex, Vector3 position) const;

	bool WorldToNavCell(Vector3 position, int& outX, int& outZ) const;
	Vector3 NavCellToWorld(int x, int z) const;
	bool IsNavCellBlockedForCustomer(int x, int z, int selfCustomerIndex, bool includeDynamic) const;
	bool FindNearestOpenNavCell(int startX, int startZ, int& outX, int& outZ, int selfCustomerIndex, bool includeDynamic) const;

	std::vector<Vector3> FindCustomerAStarPath(
		int customerIndex,
		Vector3 start,
		Vector3 goal,
		bool includeDynamic
	);

	std::vector<Vector3> SmoothCustomerPath(const std::vector<Vector3>& path) const;

	void StartCustomerRouteToPOI(int customerIndex, int destinationIndex);
	void UpdateCustomerDynamicAvoidance(float dt);
	void DrawCustomerNavGridDebug() const;
	bool TryCustomerSidestep(int customerIndex);
	void UpdateCustomersSafely(float dt);

	bool customerSpawningEnabled = true;
	int maxCustomerCount = 5;

	float customerSpawnTimer = 0.0f;
	float customerSpawnIntervalMin = 4.0f;
	float customerSpawnIntervalMax = 8.0f;
	float nextCustomerSpawnDelay = 4.0f;

	int entryPOIIndex = -1;
	int exitPOIIndex = -1;

	void UpdateCustomerSpawner(float dt);
	bool SpawnRandomCustomer();
	int FindPOIByKind(CustomerPOIKind kind) const;
	int CountActiveCustomers() const;

	std::vector<int> FindPOIsByKind(CustomerPOIKind kind) const;
	int FindExitPOI() const;
	int FindRandomPOIByKind(CustomerPOIKind kind) const;

	int GetPOIQueueOrder(int poiIndex) const;
	bool IsQueueSlotUsedByOtherCustomer(int slotPOIIndex, int selfCustomerIndex) const;
	bool IsCounterBusy(int selfCustomerIndex) const;

	int FindFreeQueueSlot(int customerIndex) const;
	int FindBetterQueueSlotForCustomer(int customerIndex) const;

	void UpdateCustomerAI(int customerIndex, float dt);
	void DespawnPendingCustomers();

	//performance monitoring
	struct PerfStats
	{
		float updateMs = 0.0f;
		float drawMs = 0.0f;

		float customerAIUpdateMs = 0.0f;
		float customerBodySyncMs = 0.0f;
		float physicsMs = 0.0f;
		float scenePhysicsSyncMs = 0.0f;
		float interactTargetMs = 0.0f;
		float dialogueUpdateMs = 0.0f;
		float pbrUpdateMs = 0.0f;

		float heldPropRenderMs = 0.0f;
		float shadowMs = 0.0f;
		float shadowUniformUploadMs = 0.0f;
		float shadowTextureAttachMs = 0.0f;

		float worldRenderMs = 0.0f;
		float bloomRenderMs = 0.0f;

		float screenCompositeMs = 0.0f;
		float dialogueDrawMs = 0.0f;
		float editorUIMs = 0.0f;

		float drawSkyboxMs = 0.0f;
		float drawPlayerMs = 0.0f;
		float drawLevelMs = 0.0f;
		float drawCustomersMs = 0.0f;
		float drawScenePropsMs = 0.0f;
		float drawBlockoutMs = 0.0f;
		float drawOutlineMs = 0.0f;
		float drawPhysicsDebugMs = 0.0f;
		float attachShadowTextureMs = 0.0f;

		int scenePropsTotal = 0;
		int scenePropsDrawn = 0;
		int scenePropsCulled = 0;
		int shadowPropsDrawn = 0;

		float scenePropCullMs = 0.0f;
		float scenePropDrawMs = 0.0f;
		int scenePropsOcclusionCulled = 0;

		float gpuOcclusionQueryMs = 0.0f;
		int gpuOcclusionQueriesIssued = 0;
		int gpuOcclusionCulled = 0;

		float performanceOverlayMs = 0.0f;
		float worldTargetBeginMs = 0.0f;
		float worldTargetEndMs = 0.0f;

		float drawGachaInstancedMs = 0.0f;
		float drawGachaBallsMs = 0.0f;
		float drawGachaMachinesMs = 0.0f;

		float drawBasketInstancesMs = 0.0f;
		float drawEditorDebugMs = 0.0f;

		float worldKnownPartsMs = 0.0f;
		float worldUnaccountedMs = 0.0f;

		float drawKnownPartsMs = 0.0f;
		float drawUnaccountedMs = 0.0f;

		float beginDrawingMs = 0.0f;
		float endDrawingMs = 0.0f;
	};

	PerfStats perf;
	bool showPerfOverlay = false;

	void DrawPerformanceOverlay();
	bool ShouldDrawSceneProp(const SceneProp& prop, const Camera3D& cam) const;

	bool IsSphereInCameraView(
		const Camera3D& cam,
		Vector3 center,
		float radius,
		float maxDistance
	) const;

	//sound

	struct RecordPlayerController
	{
		int rootIndex = -1;      // RecordPlayer
		int diskIndex = -1;      // RecordDisk
		int handIndex = -1;      // RecordHand

		Vector3 baseDiskEditorRot{};
		Vector3 baseHandEditorRot{};

		float diskSpinDeg = 0.0f;
		float handT = 0.0f;

		float diskSpinSpeedDeg = 120.0f;

		// Raylib uses Y as vertical, so this is the Blender Z-axis equivalent.
		float handPlayYawDeg = -28.0f;
		float handAnimSpeed = 4.0f;

		bool initialized = false;
		bool playerLookingAt = false;

		float interactDistance = 3.0f;
		float audioMinDistance = 1.2f;
		float audioMaxDistance = 11.0f;

		float sourceVolume = 0.85f;
	};

	enum class RecordPlayerPlaybackMode
	{
		Sequential = 0,
		LoopCurrent = 1,
		Shuffle = 2
	};

	RecordPlayerTrackInfo ParseRecordPlayerTrackInfo(
		const std::string& line,
		std::string& outAudioPath
	) const;

	RecordPlayerController recordPlayer;

	std::vector<std::string> recordPlayerTrackPaths;
	Music recordPlayerMusic{};
	bool recordPlayerMusicLoaded = false;
	bool recordPlayerPaused = true;
	int recordPlayerTrackIndex = 0;

	RecordPlayerPlaybackMode recordPlayerPlaybackMode =
		RecordPlayerPlaybackMode::LoopCurrent;

	std::vector<RecordPlayerTrackInfo> recordPlayerTrackInfos;

	Texture2D recordPlayerCoverTexture{};
	bool recordPlayerCoverLoaded = false;

	std::string recordPlayerCurrentArtist = "Unknown Artist";
	std::string recordPlayerCurrentTitle = "Unknown Track";
	std::string recordPlayerCurrentCoverPath = "";
	std::string recordPlayerCurrentAlbum = "";

	float recordPlayerTrackInfoAlpha = 0.0f;
	float recordPlayerTrackInfoTimer = 0.0f;
	float recordPlayerTrackInfoPopupSeconds = 4.0f;
	float recordPlayerTrackInfoFadeSpeed = 6.0f;

	Music mainMenuMusic{};
	bool mainMenuMusicLoaded = false;
	bool mainMenuMusicPlaying = false;

	std::string mainMenuMusicPath =
		"Audio/RecordPlayer/toby fox - UNDERTALE Soundtrack - 23 Shop.mp3";

	float mainMenuMusicVolume = 0.75f;

	bool IsMainMenuMusicScreen(GameScreen screen) const;

	void LoadMainMenuMusic();
	void UnloadMainMenuMusic();
	void StartMainMenuMusic();
	void StopMainMenuMusic();
	void UpdateMainMenuMusic(float dt);
	void UpdateMainMenuMusicVolume();

	void ApplyRecordPlayerVisualPose();
	void ResetRecordPlayerVisualPose(bool pausePlayback);

	void LoadRecordPlayerTrackDisplayInfo(int trackIndex);
	void UnloadRecordPlayerCoverImage();
	void TriggerRecordPlayerTrackPopup();
	void UpdateRecordPlayerTrackOverlay(float dt);
	void DrawRecordPlayerTrackOverlay() const;

	bool IsRecordPlayerAnimatedPartIndex(int scenePropIndex) const;
	int FindRecordPlayerTrackIndexByFilename(const std::string& wantedName) const;
	void InitializeRecordPlayerController();
	void LoadRecordPlayerPlaylist();
	void LoadRecordPlayerTrack(int trackIndex);
	void UnloadRecordPlayerTrack();

	void UpdateRecordPlayer(float dt);
	void UpdateRecordPlayerInteraction();
	void UpdateRecordPlayerVisuals(float dt);
	void UpdateRecordPlayerMusicStream();
	void UpdateRecordPlayerMusicVolume();

	void ToggleRecordPlayerPlayPause();
	void SkipRecordPlayerTrack();
	void ToggleRecordPlayerPlaybackMode();
	const char* GetRecordPlayerPlaybackModeName() const;
	int GetNextRecordPlayerTrackIndex(bool manualSkip) const;
	void AdvanceRecordPlayerTrack(bool manualSkip);

	int FindScenePropByNodeNameExact(const std::string& nodeName) const;
	bool IsLookingAtRecordPlayer() const;
	Vector3 GetRecordPlayerAudioPosition() const;

	float masterVolume = 1.0f;
	float musicVolume = 0.8f;
	float soundFxVolume = 0.8f;
	float voiceVolume = 1.0f;

	float GetVoiceVolume() const;

	//font
	Font uiFont{};
	bool uiFontLoaded = false;
	void LoadUIFont();

	void StartCustomerDialogue(int customerIndex);
	void StartSelfDialogueLines(
		const std::vector<SelfDialogueLine>& lines,
		const std::string& scriptId
	);

	void StartSelfDialogueLines(
		const std::vector<std::string>& lines,
		const std::string& scriptId
	);

	std::string GetInspectSfxForTag(const std::string& tag) const;

// Cinematic camera
	bool IsLookingAtTriggerBox(
		const CinematicTriggerZone& trigger,
		float maxDistance
	) const;

	void UpdateLookSoundTriggers(float dt);

	std::string activeLookSoundTriggerSfxId = "";
	float activeLookSoundVolume = 0.0f;
	float activeLookSoundTargetVolume = 0.0f;
	float activeLookSoundFadeInSeconds = 0.35f;
	float activeLookSoundFadeOutSeconds = 0.50f;

	void StopActiveLookSoundImmediate();


	std::vector<CinematicTriggerZone> cinematicTriggers;
	int selectedCinematicTriggerIndex = -1;

	void UpdateCinematicTriggers(float dt);
	void DrawCinematicTriggerDebug() const;
	BoundingBox GetCinematicTriggerBounds(const CinematicTriggerZone& trigger) const;
	void ResetCinematicTriggerRuntimeState();

	void StartOpeningCinematicOnce();
	void StartOpeningCinematic();
	void UpdateOpeningCinematic(float dt);

	void StartCinematicLookAt(
		Vector3 target,
		float duration,
		bool lockPlayerMovement,
		bool syncPlayerLookAtEnd
	);

	void UpdateCinematicCamera(float dt);
	bool IsCinematicCameraActive() const;
	
	void ApplyCameraLookAt(Vector3 target, float dt, float speed);
	void SyncPlayerLookFromCameraTarget();
	void StopPlayerMovementForDialogue();

	bool openingCinematicPlayedThisGame = false;
	bool openingCinematicActive = false;
	float openingCinematicTimer = 0.0f;
	float openingCinematicDuration = 4.5f;

	Vector3 openingLookUpTarget{ 0.0f, 0.0f, 0.0f };
	Vector3 openingLookDownTarget{ 0.0f, 0.0f, 0.0f };

	struct CinematicCameraState
	{
		bool active = false;
		bool lockPlayerMovement = true;
		bool syncPlayerLookAtEnd = true;

		float timer = 0.0f;
		float duration = 1.5f;

		Vector3 startTarget{ 0.0f, 0.0f, 0.0f };
		Vector3 endTarget{ 0.0f, 0.0f, 0.0f };
	};

	CinematicCameraState cinematicCamera;

	//customer camera
	
	bool customerDialogueFocusActive = false;
	int customerDialogueFocusIndex = -1;

	float customerDialogueCenterYaw = 0.0f;
	float customerDialogueCenterPitch = 0.0f;

	float customerDialogueIdleTimer = 0.0f;

	float customerDialogueYawLimitDeg = 70.0f;
	float customerDialoguePitchLimitDeg = 60.0f;

	float customerDialogueReturnDelay = 1.25f;
	float customerDialogueInitialFocusSpeed = 6.0f;
	float customerDialogueReturnSpeed = 3.5f;

	void StartCustomerDialogueFocus(int customerIndex);
	void UpdateCustomerDialogueFocus(float dt);
	void EndCustomerDialogueFocus();

	bool customerDialogueLookInputThisFrame = false;

	void UpdateCustomerDialogueLookInputOnly();

	Vector2 GetLookAnglesToWorldPoint(Vector3 target) const;
	float AngleDeltaRad(float a, float b) const;

	//storeday
	enum class StoreDayState
	{
		Closed,
		Open,
		Closing,
		Results
	};

	StoreDayState storeDayState = StoreDayState::Closed;

	struct StoreDayConfig
	{
		int day = 1;

		int customerTarget = 5;

		int minBuyerSalesYen = 0;
		int minSellerBuyYen = 0;

		float spawnDelayMin = 10.0f;
		float spawnDelayMax = 16.0f;

		int sellerChancePercent = 35;
	};

	int currentStoreDay = 1;
	int maxStoreDays = 5;

	int buyerSalesToday = 0;
	int sellerBuySpendToday = 0;
	int sellerPurchasesToday = 0;

	float dayResultTimer = 0.0f;
	float dayResultAutoAdvanceDelay = 4.0f;

	bool campaignComplete = false;

	bool dayIntroActive = false;
	float dayIntroTimer = 0.0f;
	float dayIntroDuration = 5.0f;
	float dayIntroFadeDuration = 1.2f;

	bool pendingDayIntroAfterOpeningCinematic = false;
	void StartDayIntro();
	void UpdateDayIntro(float dt);
	void DrawDayIntroOverlay() const;

	std::vector<std::string> sellerTradeItemsUsedToday;

	StoreDayConfig GetStoreDayConfig(int day) const;
	StoreDayConfig GetCurrentStoreDayConfig() const;

	float GetCurrentDaySpawnDelay() const;

	void StartDay(int day);
	void AdvanceAfterDayResult();

	bool DidPassCurrentDay() const;

	bool HasSellerTradeItemBeenUsedToday(const std::string& itemId) const;

	const CustomerTradeItemDef* PickSellerTradeItemDefNoRepeatForToday() const;

	int customersToProcessBeforeClose = 15;
	int customersSpawnedToday = 0;
	int customersCompletedToday = 0;

	int storeBudgetAtOpen = 0;

	bool finalScoreVisible = false;
	std::string closeShopReason;

	bool IsStoreOpenControlProp(const SceneProp& prop) const;
	bool IsStoreCloseControlProp(const SceneProp& prop) const;
	bool IsStoreControlProp(const SceneProp& prop) const;

	void HandleStoreControlInteraction();

	mutable std::vector<int> storeControlScenePropIndices;
	mutable int storeControlScenePropCacheSceneCount = -1;
	mutable bool storeControlScenePropCacheDirty = true;

	void MarkStoreControlScenePropCacheDirty();
	void RebuildStoreControlScenePropCache() const;
	int FindLookedAtStoreControlProp() const;

	void OpenStore();
	void BeginStoreClosing(const std::string& reason);
	void UpdateStoreDayState(float dt = 0.0f);
	int CalculateFinalScore() const;

	void DrawStoreStatusHUD() const;
	void DrawFinalScoreOverlay() const;

	bool campaignCompleteScreenVisible = false;
	bool campaignCompleteDebugTriggered = false;

	void ShowCampaignCompleteScreen(bool debugTriggered = false);
	void UpdateCampaignCompleteScreen();
	void DrawCampaignCompleteScreen() const;

	struct StoreDayResult
	{
		bool valid = false;

		std::string reason = "Day complete.";

		int day = 1;
		bool passed = false;
		bool campaignComplete = false;

		int customersSpawned = 0;
		int customersTarget = 0;
		int customersCompleted = 0;

		int startingBudgetYen = 0;
		int endingBudgetYen = 0;

		int profitYen = 0;

		int buyerSalesYen = 0;
		int buyerSalesTargetYen = 0;

		int sellerBuySpendYen = 0;
		int sellerBuyTargetYen = 0;
		int sellerPurchases = 0;

		int customerBonus = 0;
		int finalScore = 0;
	};

	StoreDayResult finalDayResult{};

	void CaptureFinalDayResult();

	void RequestWebFullscreen();

	void DrawModelMatrixFallback(Model& model, Matrix transform, Shader shader);

	bool pendingWebPointerRecapture = false;

	bool mainMenuMusicFadingOut = false;
	float mainMenuMusicFadeTimer = 0.0f;
	float mainMenuMusicFadeDuration = 0.35f;
	float mainMenuMusicFadeStartVolume = 1.0f;

};
