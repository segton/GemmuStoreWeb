#include "Game.h"
#include "Settings.h"
#include "imgui.h"
#include "rlImGui.h"
#include <cstring>
#include <fstream>
#include <iomanip>
#include <unordered_map>
#include <cmath>
#include <limits>
#include <algorithm>
#include <queue>
#include <cfloat>
#include "ThirdParty/cgltf.h"
#include <functional>
#include "external/glad.h"
#include <sstream>
#include "rlgl.h"



#if defined(_WIN32)

// Ask NVIDIA Optimus laptops to use the high-performance NVIDIA GPU.
extern "C"
{
	__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;

	// Also helps on AMD switchable graphics laptops.
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

#endif
#if defined(_WIN32)
#pragma comment(lib, "user32.lib")
extern "C" __declspec(dllimport) void __stdcall DisableProcessWindowsGhosting(void);
#endif

#ifndef RL_MIN
#define RL_MIN 0x8007
#endif
#ifndef GL_ANY_SAMPLES_PASSED_CONSERVATIVE
#define GL_ANY_SAMPLES_PASSED_CONSERVATIVE 0x8D6A
#endif

#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif

#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif



static float MoveTowardsFloatLocal(float current, float target, float maxDelta)
{
	if (current < target)
		return fminf(current + maxDelta, target);

	if (current > target)
		return fmaxf(current - maxDelta, target);

	return current;
}

static std::string SanitizeVoiceToken(std::string value)
{
	std::transform(
		value.begin(),
		value.end(),
		value.begin(),
		[](unsigned char c) { return (char)std::tolower(c); }
	);

	std::string result;
	result.reserve(value.size());

	bool previousUnderscore = false;

	for (char c : value)
	{
		bool valid =
			(c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9');

		if (valid)
		{
			result.push_back(c);
			previousUnderscore = false;
		}
		else if (!previousUnderscore)
		{
			result.push_back('_');
			previousUnderscore = true;
		}
	}

	while (!result.empty() && result.front() == '_')
		result.erase(result.begin());

	while (!result.empty() && result.back() == '_')
		result.pop_back();

	if (result.empty())
		result = "self";

	return result;
}

static std::string MakeVoicePathFromPrefix(
	const std::string& voicePrefix,
	int nodeIndex
)
{
	std::string cleanPrefix = SanitizeVoiceToken(voicePrefix);

	return TextFormat(
		"Audio/Voices/%s_n%02i.mp3",
		cleanPrefix.c_str(),
		nodeIndex
	);
}
static unsigned int HashUInt(unsigned int x)
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

static bool ShouldGachaMachineHaveBalls(int machineIndex)
{
	// 0 to 99
	unsigned int value = HashUInt((unsigned int)machineIndex + 1337U) % 100U;

	// Change this number:
	// 100 = all machines have balls
	// 50  = about half have balls
	// 35  = about one third have balls
	const unsigned int filledMachinePercent = 35U;

	return value < filledMachinePercent;
}

static Vector3 GetMatrixTranslationSafe(const Matrix& m)
{
	return { m.m12, m.m13, m.m14 };
}


static void SetShaderValueIfValid(
	Shader shader,
	int loc,
	const void* value,
	int uniformType
)
{
	if (loc >= 0)
	{
		SetShaderValue(shader, loc, value, uniformType);
	}
}


static bool ContainsInsensitiveStoreLocal(std::string text, const char* needle)
{
	std::transform(
		text.begin(),
		text.end(),
		text.begin(),
		[](unsigned char c) { return (char)std::tolower(c); }
	);

	std::string n = needle;

	std::transform(
		n.begin(),
		n.end(),
		n.begin(),
		[](unsigned char c) { return (char)std::tolower(c); }
	);

	return text.find(n) != std::string::npos;
}

static bool StartsWithText(const std::string& text, const char* prefix)
{
	return text.rfind(prefix, 0) == 0;
}

static float RectAreaSafe(Rectangle r)
{
	if (r.width <= 0.0f || r.height <= 0.0f)
		return 0.0f;

	return r.width * r.height;
}
static float SafeNonZeroScale(float value)
{
	if (!std::isfinite(value))
		return 1.0f;

	if (fabsf(value) < 0.0001f)
		return 1.0f;

	return value;
}
static Rectangle IntersectRectSafe(Rectangle a, Rectangle b)
{
	float x1 = fmaxf(a.x, b.x);
	float y1 = fmaxf(a.y, b.y);
	float x2 = fminf(a.x + a.width, b.x + b.width);
	float y2 = fminf(a.y + a.height, b.y + b.height);

	if (x2 <= x1 || y2 <= y1)
	{
		return { 0.0f, 0.0f, 0.0f, 0.0f };
	}

	return {
		x1,
		y1,
		x2 - x1,
		y2 - y1
	};
}

static Rectangle ClampRectToScreen(Rectangle r, int screenW, int screenH)
{
	float x1 = Clamp(r.x, 0.0f, (float)screenW);
	float y1 = Clamp(r.y, 0.0f, (float)screenH);
	float x2 = Clamp(r.x + r.width, 0.0f, (float)screenW);
	float y2 = Clamp(r.y + r.height, 0.0f, (float)screenH);

	if (x2 <= x1 || y2 <= y1)
	{
		return { 0.0f, 0.0f, 0.0f, 0.0f };
	}

	return {
		x1,
		y1,
		x2 - x1,
		y2 - y1
	};
}

static bool ShouldScenePropCastShadowByName(const std::string& name)
{
	if (name.empty())
		return true;

	std::string n = name;

	std::transform(
		n.begin(),
		n.end(),
		n.begin(),
		[](unsigned char c) { return (char)std::tolower(c); }
	);

	if (n.find("roof") != std::string::npos) return false;
	if (n.find("ceiling") != std::string::npos) return false;
	if (n.find("light") != std::string::npos) return false;
	if (n.find("lamp") != std::string::npos) return false;
	if (n.find("bulb") != std::string::npos) return false;
	if (n.find("no_shadow") != std::string::npos) return false;

	return true;
}
static Matrix MakeMatrixFromTRS(Vector3 pos, Quaternion rot, Vector3 scale)
{
	Matrix s = MatrixScale(scale.x, scale.y, scale.z);
	Matrix r = QuaternionToMatrix(rot);
	Matrix t = MatrixTranslate(pos.x, pos.y, pos.z);

	return MatrixMultiply(MatrixMultiply(s, r), t);
}

static Matrix MakeInstanceTRS(Vector3 pos, Vector3 rotDeg, Vector3 scale)
{
	Quaternion q = QuaternionFromEuler(
		rotDeg.x * DEG2RAD,
		rotDeg.y * DEG2RAD,
		rotDeg.z * DEG2RAD
	);

	return MakeMatrixFromTRS(pos, q, scale);
}
static float MsSince(double startTime)
{
	return (float)((GetTime() - startTime) * 1000.0);
}

struct ImportedMaterialRenderSettings
{
	int alphaMode = 0;              // 0 opaque, 1 mask, 2 blend
	float alphaCutoff = 0.5f;
	float baseAlpha = 1.0f;
	float reflectionStrength = 0.025f;
};

static void ExpandBoundsWithPoint(BoundingBox& bounds, Vector3 p, bool& hasBounds)
{
	if (!hasBounds)
	{
		bounds.min = p;
		bounds.max = p;
		hasBounds = true;
		return;
	}

	bounds.min.x = fminf(bounds.min.x, p.x);
	bounds.min.y = fminf(bounds.min.y, p.y);
	bounds.min.z = fminf(bounds.min.z, p.z);

	bounds.max.x = fmaxf(bounds.max.x, p.x);
	bounds.max.y = fmaxf(bounds.max.y, p.y);
	bounds.max.z = fmaxf(bounds.max.z, p.z);
}

static bool StartsWith(const std::string& text, const std::string& prefix)
{
	return text.rfind(prefix, 0) == 0;
}
static bool ShouldImportedNodeBeAggregateProp(const std::string& name)
{
	return StartsWith(name, "PROP_");
}
static bool ShouldImportedNodeHaveCollision(const std::string& name)
{
	return StartsWith(name, "COL_") ||
		StartsWith(name, "WALL_") ||
		StartsWith(name, "FLOOR_") ||
		StartsWith(name, "BLOCK_");
}

static bool IsFiniteFloat(float v)
{
	return std::isfinite(v);
}

static bool IsFiniteVector3(Vector3 v)
{
	return IsFiniteFloat(v.x) && IsFiniteFloat(v.y) && IsFiniteFloat(v.z);
}

static bool IsUsableJoltHalfExtents(Vector3 halfExtents)
{
	if (!IsFiniteVector3(halfExtents))
		return false;

	const float minHalf = 0.005f;
	const float maxHalf = 50.0f;

	if (halfExtents.x < minHalf) return false;
	if (halfExtents.y < minHalf) return false;
	if (halfExtents.z < minHalf) return false;

	if (halfExtents.x > maxHalf) return false;
	if (halfExtents.y > maxHalf) return false;
	if (halfExtents.z > maxHalf) return false;

	return true;
}

static std::string TrimString(const std::string& s)
{
	size_t start = s.find_first_not_of(" \t\r\n");
	size_t end = s.find_last_not_of(" \t\r\n");

	if (start == std::string::npos)
		return "";

	return s.substr(start, end - start + 1);
}

static bool GroupMatches(const std::string& poiGroupRaw, const std::string& customerGroupRaw)
{
	std::string poiGroup = TrimString(poiGroupRaw);
	std::string customerGroup = TrimString(customerGroupRaw);

	if (poiGroup.empty() || poiGroup == "any" || poiGroup == "all")
		return true;

	if (customerGroup.empty())
		return false;

	size_t start = 0;

	while (start < poiGroup.size())
	{
		size_t comma = poiGroup.find(',', start);

		std::string token = comma == std::string::npos
			? poiGroup.substr(start)
			: poiGroup.substr(start, comma - start);

		token = TrimString(token);

		if (token == customerGroup)
			return true;

		if (comma == std::string::npos)
			break;

		start = comma + 1;
	}

	return false;
}

static BoundingBox MakeCustomerCollisionBoxAt(Vector3 position)
{
	const float radius = 0.28f;
	const float height = 1.75f;

	Vector3 center = {
		position.x,
		position.y + height * 0.5f,
		position.z
	};

	BoundingBox box{};
	box.min = {
		center.x - radius,
		center.y - height * 0.5f,
		center.z - radius
	};

	box.max = {
		center.x + radius,
		center.y + height * 0.5f,
		center.z + radius
	};

	return box;
}


static BoundingBox MakePlayerCollisionBoxAt(Vector3 position)
{
	const float radius = 0.35f;
	const float height = 1.75f;

	Vector3 center = {
		position.x,
		position.y + height * 0.5f,
		position.z
	};

	BoundingBox box{};
	box.min = {
		center.x - radius,
		center.y - height * 0.5f,
		center.z - radius
	};

	box.max = {
		center.x + radius,
		center.y + height * 0.5f,
		center.z + radius
	};

	return box;
}

static bool ModelHasRealTexture(const Model& model, int mapIndex)
{
	for (int i = 0; i < model.materialCount; i++)
	{
		if (model.materials[i].maps[mapIndex].texture.id > 1)
		{
			return true;
		}
	}

	return false;
}
static Vector3 GetCustomerColliderCenter(const Customer& customer)
{
	return {
		customer.position.x,
		customer.position.y + 0.875f,
		customer.position.z
	};
}

bool Game::PlayerWouldOverlapCustomer(Vector3 playerPosition) const
{
	BoundingBox playerBox = MakePlayerCollisionBoxAt(playerPosition);

	for (const Customer& customer : customers)
	{
		if (customer.pendingDespawn)
			continue;

		BoundingBox customerBox = MakeCustomerCollisionBoxAt(customer.position);

		if (CheckCollisionBoxes(playerBox, customerBox))
			return true;
	}

	return false;
}

void Game::MoveVirtualPlayerToPosition(Vector3 playerPosition)
{
	player.m_pos = playerPosition;

	if (!playerCharacter)
		return;

	playerCharacter->SetPosition(
		JPH::RVec3(
			playerPosition.x,
			playerPosition.y + playerHeight * 0.5f,
			playerPosition.z
		)
	);

	playerCharacter->SetLinearVelocity(JPH::Vec3::sZero());

	player.m_velocity = { 0.0f, 0.0f, 0.0f };
}

static Quaternion GetCustomerColliderRotation(const Customer& customer)
{
	// Box is mostly square, so yaw is not critical yet.
	// Keep it ready for later if you make the collider rectangular.
	return QuaternionFromEuler(
		0.0f,
		customer.yawDeg * DEG2RAD,
		0.0f
	);
}

static size_t GetNextUtf8CharOffset(const std::string& text, size_t offset)
{
	if (offset >= text.size()) return text.size();

	unsigned char c = (unsigned char)text[offset];

	if ((c & 0x80) == 0x00) return offset + 1; // 1-byte ASCII
	if ((c & 0xE0) == 0xC0) return std::min(offset + 2, text.size());
	if ((c & 0xF0) == 0xE0) return std::min(offset + 3, text.size());
	if ((c & 0xF8) == 0xF0) return std::min(offset + 4, text.size());

	return offset + 1;
}

static size_t GetUtf8ByteOffsetForCharCount(const std::string& text, int charCount)
{
	size_t offset = 0;

	for (int i = 0; i < charCount && offset < text.size(); i++)
	{
		offset = GetNextUtf8CharOffset(text, offset);
	}

	return offset;
}

static bool RayIntersectsBoundingBoxWithinDistance(
	Ray ray,
	BoundingBox box,
	float maxDistance,
	float& outDistance
)
{
	RayCollision hit = GetRayCollisionBox(ray, box);

	if (!hit.hit) return false;
	if (hit.distance > maxDistance) return false;

	outDistance = hit.distance;
	return true;
}

static void GetModelLocalBoundsCollider(
	Model* model,
	Vector3& outSize,
	Vector3& outOffset
)
{
	if (model == nullptr)
	{
		outSize = { 1.0f, 1.0f, 1.0f };
		outOffset = { 0.0f, 0.0f, 0.0f };
		return;
	}

	BoundingBox bounds = GetModelBoundingBox(*model);

	Vector3 localSize = {
		bounds.max.x - bounds.min.x,
		bounds.max.y - bounds.min.y,
		bounds.max.z - bounds.min.z
	};

	Vector3 localCenter = {
		(bounds.min.x + bounds.max.x) * 0.5f,
		(bounds.min.y + bounds.max.y) * 0.5f,
		(bounds.min.z + bounds.max.z) * 0.5f
	};

	const float minSize = 0.01f;

	outSize = {
		fmaxf(localSize.x, minSize),
		fmaxf(localSize.y, minSize),
		fmaxf(localSize.z, minSize)
	};

	outOffset = localCenter;

}
static void AttachShadowTextureToModel(Model& model, Texture2D shadowTexture)
{
	if (shadowTexture.id == 0) return;
	if (model.materials == nullptr) return;

	for (int i = 0; i < model.materialCount; i++)
	{
		model.materials[i].maps[MATERIAL_MAP_HEIGHT].texture = shadowTexture;
	}
}

static void BindTextureToShaderSlot(
	Shader shader,
	const char* uniformName,
	Texture2D texture,
	int slot
)
{
	int loc = GetShaderLocation(shader, uniformName);

	if (loc < 0)
	{
		TraceLog(LOG_WARNING, "Shader uniform not found: %s", uniformName);
		return;
	}

	if (texture.id == 0)
	{
		TraceLog(LOG_WARNING, "Texture id is 0 for uniform: %s", uniformName);
		return;
	}

	rlEnableShader(shader.id);

	rlActiveTextureSlot(slot);
	rlEnableTexture(texture.id);
	rlSetUniformSampler(loc, slot);

	rlActiveTextureSlot(0);

	rlDisableShader();

	static bool logged = false;
	if (!logged)
	{
		TraceLog(
			LOG_INFO,
			"Bound %s texture id %u to slot %i, shader=%u, loc=%i",
			uniformName,
			texture.id,
			slot,
			shader.id,
			loc
		);

		logged = true;
	}
}



const Game::OutlineModelCache* Game::GetOutlineModelCache(const Model* model) const
{
	for (const OutlineModelCache& cache : outlineModelCaches)
	{
		if (cache.source == model)
			return &cache;
	}

	return nullptr;
}

void Game::UnloadOutlineModelCaches()
{
	for (OutlineModelCache& cache : outlineModelCaches)
	{
		for (Mesh& mesh : cache.meshes)
		{
			UnloadMesh(mesh);
		}

		cache.meshes.clear();
	}

	outlineModelCaches.clear();
}

struct SmoothKey
{
	int x;
	int y;
	int z;

	bool operator==(const SmoothKey& other) const
	{
		return x == other.x && y == other.y && z == other.z;
	}
};

struct SmoothKeyHash
{
	std::size_t operator()(const SmoothKey& key) const
	{
		std::size_t h1 = std::hash<int>()(key.x);
		std::size_t h2 = std::hash<int>()(key.y);
		std::size_t h3 = std::hash<int>()(key.z);

		return h1 ^ (h2 << 1) ^ (h3 << 2);
	}
};

static SmoothKey MakeSmoothKey(Vector3 p)
{
	const float scale = 10000.0f;

	return {
		(int)roundf(p.x * scale),
		(int)roundf(p.y * scale),
		(int)roundf(p.z * scale)
	};
}

static Vector3 GetMeshVertex(const Mesh& mesh, int index)
{
	return {
		mesh.vertices[index * 3 + 0],
		mesh.vertices[index * 3 + 1],
		mesh.vertices[index * 3 + 2]
	};
}

static Vector3 GetMeshNormal(const Mesh& mesh, int index)
{
	if (mesh.normals == nullptr)
		return { 0.0f, 1.0f, 0.0f };

	return {
		mesh.normals[index * 3 + 0],
		mesh.normals[index * 3 + 1],
		mesh.normals[index * 3 + 2]
	};
}

static void StoreSmoothNormalsInTangents(Mesh& mesh)
{
	if (mesh.vertexCount <= 0 || mesh.vertices == nullptr)
		return;

	if (mesh.tangents != nullptr)
	{
		MemFree(mesh.tangents);
		mesh.tangents = nullptr;
	}

	mesh.tangents = (float*)MemAlloc(sizeof(float) * mesh.vertexCount * 4);

	std::unordered_map<SmoothKey, Vector3, SmoothKeyHash> normalSums;
	std::unordered_map<SmoothKey, int, SmoothKeyHash> normalCounts;

	for (int i = 0; i < mesh.vertexCount; i++)
	{
		Vector3 p = GetMeshVertex(mesh, i);
		Vector3 n = GetMeshNormal(mesh, i);

		SmoothKey key = MakeSmoothKey(p);

		normalSums[key] = Vector3Add(normalSums[key], n);
		normalCounts[key]++;
	}

	for (int i = 0; i < mesh.vertexCount; i++)
	{
		Vector3 p = GetMeshVertex(mesh, i);
		SmoothKey key = MakeSmoothKey(p);

		Vector3 smooth = normalSums[key];

		if (Vector3Length(smooth) > 0.0001f)
			smooth = Vector3Normalize(smooth);
		else
			smooth = GetMeshNormal(mesh, i);

		mesh.tangents[i * 4 + 0] = smooth.x;
		mesh.tangents[i * 4 + 1] = smooth.y;
		mesh.tangents[i * 4 + 2] = smooth.z;
		mesh.tangents[i * 4 + 3] = 0.0f;
	}
}

static Mesh CloneMeshForOutline(const Mesh& src)
{
	Mesh dst{};

	dst.vertexCount = src.vertexCount;
	dst.triangleCount = src.triangleCount;

	if (src.vertices != nullptr)
	{
		dst.vertices = (float*)MemAlloc(sizeof(float) * src.vertexCount * 3);
		memcpy(dst.vertices, src.vertices, sizeof(float) * src.vertexCount * 3);
	}

	if (src.normals != nullptr)
	{
		dst.normals = (float*)MemAlloc(sizeof(float) * src.vertexCount * 3);
		memcpy(dst.normals, src.normals, sizeof(float) * src.vertexCount * 3);
	}

	if (src.indices != nullptr)
	{
		dst.indices = (unsigned short*)MemAlloc(sizeof(unsigned short) * src.triangleCount * 3);
		memcpy(dst.indices, src.indices, sizeof(unsigned short) * src.triangleCount * 3);
	}

	StoreSmoothNormalsInTangents(dst);

	UploadMesh(&dst, false);

	return dst;
}

RenderTexture2D LoadShadowMapRenderTexture(int width, int height)
{
	RenderTexture2D target = { 0 };

	target.id = rlLoadFramebuffer();

	if (target.id > 0)
	{
		rlEnableFramebuffer(target.id);

		// Color texture: this will store encoded depth from shadow_depth.fs
		target.texture.id = rlLoadTexture(
			nullptr,
			width,
			height,
			PIXELFORMAT_UNCOMPRESSED_R32,
			1
		);

		target.texture.format = PIXELFORMAT_UNCOMPRESSED_R32;

		target.texture.width = width;
		target.texture.height = height;
		target.texture.mipmaps = 1;


		// Depth attachment: only for correct depth testing during shadow rendering
		target.depth.id = rlLoadTextureDepth(width, height, false);
		target.depth.width = width;
		target.depth.height = height;
		target.depth.mipmaps = 1;
		target.depth.format = PIXELFORMAT_UNCOMPRESSED_R32;

		rlFramebufferAttach(
			target.id,
			target.texture.id,
			RL_ATTACHMENT_COLOR_CHANNEL0,
			RL_ATTACHMENT_TEXTURE2D,
			0
		);

		rlFramebufferAttach(
			target.id,
			target.depth.id,
			RL_ATTACHMENT_DEPTH,
			RL_ATTACHMENT_TEXTURE2D,
			0
		);

		if (!rlFramebufferComplete(target.id))
		{
			TraceLog(LOG_WARNING, "Shadow map framebuffer is not complete");
		}

		rlDisableFramebuffer();
	}

	return target;
}

static float ColorByteToFloat(unsigned char value)
{
	return (float)value / 255.0f;
}

static unsigned char FloatToColorByte(float value)
{
	value = Clamp(value, 0.0f, 1.0f);
	return (unsigned char)(value * 255.0f + 0.5f);
}

static bool DrawRaylibColorEdit4(const char* label, Color& color)
{
	float values[4] = {
		ColorByteToFloat(color.r),
		ColorByteToFloat(color.g),
		ColorByteToFloat(color.b),
		ColorByteToFloat(color.a)
	};

	bool changed = ImGui::ColorEdit4(label, values, ImGuiColorEditFlags_AlphaBar);

	if (changed)
	{
		color.r = FloatToColorByte(values[0]);
		color.g = FloatToColorByte(values[1]);
		color.b = FloatToColorByte(values[2]);
		color.a = FloatToColorByte(values[3]);
	}

	return changed;

}
void DrawModelWithShader(
	Model& model,
	Vector3 position,
	Vector3 rotationAxis,
	float rotationAngle,
	Vector3 scale,
	Shader shader
)
{
	Shader oldShaders[32];

	int count = model.materialCount;
	if (count > 32) count = 32;

	for (int i = 0; i < count; i++)
	{
		oldShaders[i] = model.materials[i].shader;
		model.materials[i].shader = shader;
	}

	DrawModelEx(model, position, rotationAxis, rotationAngle, scale, WHITE);

	for (int i = 0; i < count; i++)
	{
		model.materials[i].shader = oldShaders[i];
	}
}
static bool ContainsMaterialIndex(const std::vector<int>& indices, int materialIndex)
{
	for (int i : indices)
	{
		if (i == materialIndex) return true;
	}
	return false;
}

static Matrix BuildModelMatrix(Vector3 pos, Vector3 rotDeg, Vector3 scale)
{
	Quaternion q = QuaternionFromEuler(
		rotDeg.x * DEG2RAD,
		rotDeg.y * DEG2RAD,
		rotDeg.z * DEG2RAD
	);

	Matrix transform = MatrixIdentity();
	transform = MatrixMultiply(transform, MatrixScale(scale.x, scale.y, scale.z));
	transform = MatrixMultiply(transform, QuaternionToMatrix(q));
	transform = MatrixMultiply(transform, MatrixTranslate(pos.x, pos.y, pos.z));
	return transform;
}

static TextureCubemap GenTextureCubemap(Shader shader, Texture2D panorama, int size, int format)
{
	TextureCubemap cubemap = { 0 };

	rlDisableBackfaceCulling();

	unsigned int rbo = rlLoadTextureDepth(size, size, true);
	cubemap.id = rlLoadTextureCubemap(0, size, format, 1);

	unsigned int fbo = rlLoadFramebuffer();
	rlFramebufferAttach(fbo, rbo, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
	rlFramebufferAttach(fbo, cubemap.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_CUBEMAP_POSITIVE_X, 0);

	rlEnableShader(shader.id);

	Matrix matFboProjection = MatrixPerspective(90.0 * DEG2RAD, 1.0f, rlGetCullDistanceNear(), rlGetCullDistanceFar());
	rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_PROJECTION], matFboProjection);

	Matrix fboViews[6] = {
		MatrixLookAt(Vector3 { 0.0f, 0.0f, 0.0f }, Vector3{ 1.0f,  0.0f,  0.0f }, Vector3 { 0.0f, -1.0f,  0.0f }),
		MatrixLookAt(Vector3 { 0.0f, 0.0f, 0.0f }, Vector3{ -1.0f,  0.0f,  0.0f }, Vector3 { 0.0f, -1.0f,  0.0f }),
		MatrixLookAt(Vector3 { 0.0f, 0.0f, 0.0f }, Vector3{ 0.0f,  1.0f,  0.0f }, Vector3 { 0.0f,  0.0f,  1.0f }),
		MatrixLookAt(Vector3 { 0.0f, 0.0f, 0.0f }, Vector3 { 0.0f, -1.0f,  0.0f }, Vector3 { 0.0f,  0.0f, -1.0f }),
		MatrixLookAt(Vector3 { 0.0f, 0.0f, 0.0f }, Vector3 { 0.0f,  0.0f,  1.0f }, Vector3 { 0.0f, -1.0f,  0.0f }),
		MatrixLookAt(Vector3 { 0.0f, 0.0f, 0.0f }, Vector3 { 0.0f,  0.0f, -1.0f }, Vector3 { 0.0f, -1.0f,  0.0f })
	};

	rlViewport(0, 0, size, size);

	rlActiveTextureSlot(0);
	rlEnableTexture(panorama.id);

	for (int i = 0; i < 6; i++)
	{
		rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_VIEW], fboViews[i]);

		rlFramebufferAttach(fbo, cubemap.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_CUBEMAP_POSITIVE_X + i, 0);
		rlEnableFramebuffer(fbo);

		rlClearScreenBuffers();
		rlLoadDrawCube();
	}

	rlDisableShader();
	rlDisableTexture();
	rlDisableFramebuffer();
	rlUnloadFramebuffer(fbo);

	rlViewport(0, 0, rlGetFramebufferWidth(), rlGetFramebufferHeight());
	rlEnableBackfaceCulling();

	cubemap.width = size;
	cubemap.height = size;
	cubemap.mipmaps = 1;
	cubemap.format = format;

	return cubemap;
}

static bool DrawEditableName(const char* label, std::string& value)
{
	char buffer[128] = {};
	strncpy_s(buffer, value.c_str(), sizeof(buffer) - 1);

	if (ImGui::InputText(label, buffer, sizeof(buffer)))
	{
		value = buffer;
		return true;
	}

	return false;
}


static void SetTextureUsage(Shader shader, int useAlbedo, int useNormal, int useMRA, int useEmissive)
{
	int loc = GetShaderLocation(shader, "useTexAlbedo");
	SetShaderValue(shader, loc, &useAlbedo, SHADER_UNIFORM_INT);

	loc = GetShaderLocation(shader, "useTexNormal");
	SetShaderValue(shader, loc, &useNormal, SHADER_UNIFORM_INT);

	loc = GetShaderLocation(shader, "useTexMRA");
	SetShaderValue(shader, loc, &useMRA, SHADER_UNIFORM_INT);

	loc = GetShaderLocation(shader, "useTexEmissive");
	SetShaderValue(shader, loc, &useEmissive, SHADER_UNIFORM_INT);
}
static Vector3 NormalizeSafe(Vector3 v)
{
	float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
	if (len <= 0.0001f) return { 0.0f, 0.0f, 1.0f };

	return { v.x / len, v.y / len, v.z / len };
}
static Quaternion QuaternionFromEulerDeg(Vector3 eulerDeg)
{
	return QuaternionFromEuler(
		eulerDeg.x * DEG2RAD,
		eulerDeg.y * DEG2RAD,
		eulerDeg.z * DEG2RAD
	);
}

static void QuaternionToAxisAngleSafe(Quaternion q, Vector3& axis, float& angleDeg)
{
	q = QuaternionNormalize(q);

	float angle = 2.0f * acosf(q.w);
	float s = sqrtf(1.0f - q.w * q.w);

	if (s < 0.0001f)
		axis = { 0.0f, 1.0f, 0.0f };
	else
		axis = { q.x / s, q.y / s, q.z / s };

	angleDeg = angle * RAD2DEG;
}

static void DrawModelEuler(Model model, Vector3 pos, Vector3 rotDeg, Vector3 scale, Color tint)
{
	Quaternion q = QuaternionFromEulerDeg(rotDeg);

	Vector3 axis;
	float angleDeg;
	QuaternionToAxisAngleSafe(q, axis, angleDeg);

	DrawModelEx(model, pos, axis, angleDeg, scale, tint);
}
static void DrawModelEulerWithShader(
	Model& model,
	Vector3 pos,
	Vector3 rotDeg,
	Vector3 scale,
	Shader shader
)
{
	Quaternion q = QuaternionFromEulerDeg(rotDeg);

	Vector3 axis;
	float angleDeg;
	QuaternionToAxisAngleSafe(q, axis, angleDeg);

	DrawModelWithShader(
		model,
		pos,
		axis,
		angleDeg,
		scale,
		shader
	);
}


Game::Game()
{
#if defined(_WIN32)
	DisableProcessWindowsGhosting();
#endif

	InitWindow(1280, 720, "JapanSim");

	const unsigned char* glVendor = glGetString(GL_VENDOR);
	const unsigned char* glRenderer = glGetString(GL_RENDERER);
	const unsigned char* glVersion = glGetString(GL_VERSION);

	TraceLog(LOG_INFO, "OpenGL GPU Vendor: %s", glVendor ? (const char*)glVendor : "Unknown");
	TraceLog(LOG_INFO, "OpenGL GPU Renderer: %s", glRenderer ? (const char*)glRenderer : "Unknown");
	TraceLog(LOG_INFO, "OpenGL Version: %s", glVersion ? (const char*)glVersion : "Unknown");

#ifndef _DEBUG
	SetTraceLogLevel(LOG_NONE);
#endif

	InitAudioDevice();

	LoadMainMenuMusic();

	int monitor = GetCurrentMonitor();

	Vector2 monitorPos = GetMonitorPosition(monitor);
	int monitorWidth = GetMonitorWidth(monitor);
	int monitorHeight = GetMonitorHeight(monitor);

	SetWindowState(FLAG_WINDOW_UNDECORATED);
	SetWindowState(FLAG_WINDOW_TOPMOST);

	SetWindowPosition((int)monitorPos.x, (int)monitorPos.y);
	SetWindowSize(monitorWidth, monitorHeight);

	SetWindowFocused();

	rlImGuiSetup(true);

	loadingStartTime = GetTime();
	loadingStep = 0;
	loadingStepPrimed = false;
	loadingProgress = 0.0f;
	loadingStatus = "Starting...";

	screenAfterLoading = devStartDirectlyInGame
		? GameScreen::Playing
		: GameScreen::MainMenu;

	SetScreen(GameScreen::Loading);
}

void Game::ResumeGame()
{
	SetScreen(GameScreen::Playing);
}
void Game::ReturnToMainMenu()
{
	if (inspectMode)
	{
		ExitInspectMode();
	}

	if (hasHeldBody)
	{
		StopHoldingBody();
	}

	editMode = false;
	cursorUnlocked = false;
	targetedScenePropIndex = -1;

	musicBoxSceneBlackoutAmount = 0.0f;
	EndMusicBoxInspectEasterEgg();

	SetScreen(GameScreen::MainMenu);
}

void Game::FinishLoading()
{
	loadingStatus = "Ready.";
	loadingProgress = 1.0f;

	if (screenAfterLoading == GameScreen::Playing)
	{
		// The loading steps already built the world, physics, customers,
		// scene props, saved scene state, and virtual player.
		// So do not call StartNewGame() here unless you want to reload everything again.

		ResetPlayerToSpawn();

		editMode = false;
		cursorUnlocked = false;
		targetedScenePropIndex = -1;

		hasHeldBody = false;
		isHoldingBox = false;
		heldScenePropIndex = -1;
		heldBody = JPH::BodyID();

		SetWindowFocused();

		ResetCinematicTriggerRuntimeState();

		SetScreen(GameScreen::Playing);
		StartOpeningCinematicOnce();
		return;
	}

	SetScreen(GameScreen::MainMenu);
}

void Game::DrawBudgetHUD() const
{
	int screenW = GetScreenWidth();

	const float fontSize = 24.0f;
	const float spacing = 1.0f;

	Font font = uiFontLoaded ? uiFont : GetFontDefault();

	// Safer than typing ¥ directly if Game.cpp encoding is uncertain.
	std::string budgetText = TextFormat("Budget: \xC2\xA5%i", storeBudgetYen);

	Vector2 textSize = MeasureTextEx(
		font,
		budgetText.c_str(),
		fontSize,
		spacing
	);

	Rectangle bg{
		(float)screenW - textSize.x - 44.0f,
		24.0f,
		textSize.x + 28.0f,
		44.0f
	};

	DrawRectangleRounded(
		bg,
		0.25f,
		8,
		Color{ 0, 0, 0, 150 }
	);

	DrawRectangleRoundedLines(
		bg,
		0.25f,
		8,
		Color{ 255, 255, 255, 80 }
	);

	Vector2 textPos{
		bg.x + 14.0f,
		bg.y + (bg.height - fontSize) * 0.5f - 1.0f
	};

	DrawTextEx(
		font,
		budgetText.c_str(),
		textPos,
		fontSize,
		spacing,
		WHITE
	);

	if (!lastTransactionText.empty())
	{
		const float smallSize = 18.0f;

		Vector2 smallTextSize = MeasureTextEx(
			font,
			lastTransactionText.c_str(),
			smallSize,
			spacing
		);

		Color txColor = lastTransactionDeltaYen >= 0
			? Color{ 120, 255, 150, 255 }
		: Color{ 255, 170, 120, 255 };

		DrawTextEx(
			font,
			lastTransactionText.c_str(),
			Vector2{
				(float)screenW - smallTextSize.x - 30.0f,
				76.0f
			},
			smallSize,
			spacing,
			txColor
		);
	}
}
void Game::StartNewGame()
{
	if (inspectMode)
	{
		ExitInspectMode();
	}

	if (hasHeldBody)
	{
		StopHoldingBody();
	}

	openingCinematicPlayedThisGame = false;
	openingCinematicActive = false;
	cinematicCamera.active = false;

	editMode = false;
	cursorUnlocked = false;
	targetedScenePropIndex = -1;

	hasHeldBody = false;
	isHoldingBox = false;
	heldScenePropIndex = -1;
	heldBody = JPH::BodyID();

	storeBudgetYen = startingBudgetYen;
	lastTransactionDeltaYen = 0;
	lastTransactionText.clear();

	ResetPlayerToSpawn();
	BuildItemPlacementSpots();

	LoadSceneState();
	RepairScenePropHierarchyLinks();
	PreparePickupScenePropsForPhysics();
	UpdateScenePropWorldTransforms();
	ApplyImportedEditorTransformsToRuntime();
	MarkShadowTextureBindingsDirty(false);

	ResetCinematicTriggerRuntimeState();

	MarkInteractableScenePropCacheDirty();
	MarkStoreControlScenePropCacheDirty();

	InitializeRecordPlayerController();
	// Reset animation state cleanly after load.
	recordPlayer.diskSpinDeg = 0.0f;
	recordPlayer.handT = recordPlayerPaused ? 0.0f : 1.0f;

	UpdateRecordPlayerVisuals(0.0f);

	LoadRecordPlayerPlaylist();
	// Temporary debug.
	for (int i = 0; i < (int)blockoutBoxes.size(); i++)
	{
		const BlockoutBox& b = blockoutBoxes[i];

		TraceLog(
			LOG_INFO,
			"Blockout[%i] %s: hasCollision=%i blocksPlayer=%i useNormal=%i useJolt=%i pos=(%.2f %.2f %.2f) size=(%.2f %.2f %.2f)",
			i,
			b.name.c_str(),
			b.hasCollision ? 1 : 0,
			b.blocksPlayer ? 1 : 0,
			b.useNormalCollision ? 1 : 0,
			b.useJoltCollider ? 1 : 0,
			b.position.x, b.position.y, b.position.z,
			b.size.x, b.size.y, b.size.z
		);
	}

	RecreatePhysicsWorld();

	pendingPostLoadPhysicsRestore = true;
	blockoutDirty = false;
	shadowMapsDirty = true;

	for (Customer& customer : customers)
	{
		customer.SetAnimState(CustomerAnimState::Idle);
	}

	BuildCustomerTradeItemCatalog();
	BuildCustomerDialogueTree();
	BuildDialogue();
	RefreshDialogueUILayout();
	//StartOpeningCinematic();

	currentStoreDay = 1;
	campaignComplete = false;

	campaignCompleteScreenVisible = false;
	campaignCompleteDebugTriggered = false;

	storeBudgetYen = startingBudgetYen;
	lastTransactionDeltaYen = 0;
	lastTransactionText.clear();

	pendingDayIntroAfterOpeningCinematic = false;
	dayIntroActive = false;
	dayIntroTimer = 0.0f;

	StartDay(currentStoreDay);

	SetScreen(GameScreen::Playing);

	// Only day 1 gets the opening cinematic.
	StartOpeningCinematicOnce();
}

void Game::StartDayIntro()
{
	dayIntroActive = true;
	dayIntroTimer = 0.0f;
}

void Game::UpdateDayIntro(float dt)
{
	if (!dayIntroActive)
		return;

	dayIntroTimer += dt;

	if (dayIntroTimer >= dayIntroDuration)
	{
		dayIntroActive = false;
		dayIntroTimer = 0.0f;
	}
}

void Game::DrawDayIntroOverlay() const
{
	if (!dayIntroActive)
		return;

	if (finalScoreVisible)
		return;

	int screenW = GetScreenWidth();
	int screenH = GetScreenHeight();

	float alpha = 1.0f;

	if (dayIntroTimer < dayIntroFadeDuration)
	{
		alpha = dayIntroTimer / dayIntroFadeDuration;
	}
	else if (dayIntroTimer > dayIntroDuration - dayIntroFadeDuration)
	{
		alpha = (dayIntroDuration - dayIntroTimer) / dayIntroFadeDuration;
	}

	alpha = Clamp(alpha, 0.0f, 1.0f);

	unsigned char bgA = (unsigned char)(205.0f * alpha);
	unsigned char textA = (unsigned char)(255.0f * alpha);
	unsigned char subTextA = (unsigned char)(190.0f * alpha);

	DrawRectangle(
		0,
		0,
		screenW,
		screenH,
		Color{ 0, 0, 0, bgA }
	);

	Font font = uiFontLoaded ? uiFont : GetFontDefault();

	const char* dayText = TextFormat("DAY %i", currentStoreDay);

	Vector2 daySize = MeasureTextEx(
		font,
		dayText,
		72.0f,
		2.0f
	);

	DrawTextEx(
		font,
		dayText,
		Vector2{
			screenW * 0.5f - daySize.x * 0.5f,
			screenH * 0.5f - 70.0f
		},
		72.0f,
		2.0f,
		Color{ 255, 255, 255, textA }
	);

	StoreDayConfig cfg = GetCurrentStoreDayConfig();

	const char* quotaText = TextFormat(
		"Customers: %i   Sales: \xC2\xA5%i   Buys: \xC2\xA5%i",
		cfg.customerTarget,
		cfg.minBuyerSalesYen,
		cfg.minSellerBuyYen
	);

	Vector2 quotaSize = MeasureTextEx(
		font,
		quotaText,
		22.0f,
		1.0f
	);

	DrawTextEx(
		font,
		quotaText,
		Vector2{
			screenW * 0.5f - quotaSize.x * 0.5f,
			screenH * 0.5f + 20.0f
		},
		22.0f,
		1.0f,
		Color{ 190, 200, 215, subTextA }
	);
}

void Game::StartDay(int day)
{
	currentStoreDay = Clamp(day, 1, maxStoreDays);

	StoreDayConfig cfg = GetCurrentStoreDayConfig();

	storeDayState = StoreDayState::Closed;

	customerSpawningEnabled = false;

	customersToProcessBeforeClose = cfg.customerTarget;

	customersSpawnedToday = 0;
	customersCompletedToday = 0;

	buyerSalesToday = 0;
	sellerBuySpendToday = 0;
	sellerPurchasesToday = 0;

	sellerTradeItemsUsedToday.clear();

	storeBudgetAtOpen = storeBudgetYen;

	finalScoreVisible = false;
	finalDayResult = {};
	closeShopReason.clear();
	dayResultTimer = 0.0f;

	customerSpawnTimer = 0.0f;
	nextCustomerSpawnDelay = GetCurrentDaySpawnDelay();

	// Clear leftover runtime customers/items from the previous day.
	BuildCustomers();
	PurgeRuntimeCustomerItemSceneProps();

	ResetPlayerToSpawn();
	RebuildItemPlacementSpotOccupancy();

	if (currentStoreDay == 1 && !openingCinematicPlayedThisGame)
	{
		// Day 1 intro should appear after the opening cinematic/dialogue,
		// not immediately when the day is created.
		pendingDayIntroAfterOpeningCinematic = true;
	}
	else
	{
		StartDayIntro();
	}

	TraceLog(
		LOG_INFO,
		"Started day %i: target=%i salesTarget=%i sellerBuyTarget=%i spawnDelay=%.1f-%.1f",
		currentStoreDay,
		cfg.customerTarget,
		cfg.minBuyerSalesYen,
		cfg.minSellerBuyYen,
		cfg.spawnDelayMin,
		cfg.spawnDelayMax
	);
}

void Game::LoadingPulse(const char* status, float progress, bool forceDraw)
{
	loadingStatus = status;
	loadingProgress = Clamp(progress, 0.0f, 1.0f);

	// Process window messages so Windows does not think the app is dead.
	PollInputEvents();

	static double lastPulseDrawTime = 0.0;
	double now = GetTime();

	// Avoid drawing too aggressively.
	if (!forceDraw && now - lastPulseDrawTime < 0.05)
		return;

	lastPulseDrawTime = now;

	DrawLoadingScreen();

	// Pump events again after drawing.
	PollInputEvents();
}

void Game::DrawStartupLoadingScreen(const char* status, float progress, double startTime)
{
	static double lastLoadingDrawTime = 0.0;

	double now = GetTime();

	// Do not redraw more than about 20 times per second during startup.
	if (now - lastLoadingDrawTime < 0.05 && progress < 1.0f)
	{
		PollInputEvents();
		return;
	}

	lastLoadingDrawTime = now;

	progress = Clamp(progress, 0.0f, 1.0f);

	int screenW = GetScreenWidth();
	int screenH = GetScreenHeight();

	BeginDrawing();
	ClearBackground(Color{ 12, 14, 18, 255 });

	const char* title = "gemu shoppu";
	int titleSize = 56;
	int titleWidth = MeasureText(title, titleSize);

	DrawText(
		title,
		screenW / 2 - titleWidth / 2,
		screenH / 2 - 140,
		titleSize,
		WHITE
	);

	DrawText(
		"Loading...",
		screenW / 2 - MeasureText("Loading...", 28) / 2,
		screenH / 2 - 50,
		28,
		RAYWHITE
	);

	DrawText(
		status,
		screenW / 2 - MeasureText(status, 20) / 2,
		screenH / 2 - 10,
		20,
		Color{ 180, 190, 210, 255 }
	);

	float barW = 520.0f;
	float barH = 22.0f;
	float barX = screenW * 0.5f - barW * 0.5f;
	float barY = screenH * 0.5f + 40.0f;

	DrawRectangleRounded(
		{ barX, barY, barW, barH },
		0.35f,
		8,
		Color{ 35, 42, 55, 255 }
	);

	DrawRectangleRounded(
		{ barX, barY, barW * progress, barH },
		0.35f,
		8,
		Color{ 80, 140, 220, 255 }
	);

	DrawRectangleRoundedLines(
		{ barX, barY, barW, barH },
		0.35f,
		8,
		Color{ 130, 160, 210, 255 }
	);

	const char* percentText = TextFormat("%i%%", (int)(progress * 100.0f));
	DrawText(
		percentText,
		screenW / 2 - MeasureText(percentText, 20) / 2,
		(int)(barY + 34.0f),
		20,
		RAYWHITE
	);

	float elapsed = (float)(GetTime() - startTime);

	DrawText(
		TextFormat("Elapsed: %.1fs", elapsed),
		screenW / 2 - 60,
		(int)(barY + 64.0f),
		18,
		Color{ 160, 170, 190, 255 }
	);

	EndDrawing();

	PollInputEvents();
}
void Game::DrawShadowSettingsUI()
{
	if (!ImGui::CollapsingHeader("Shadow Performance"))
		return;

	if (ImGui::Checkbox("Update Shadow Maps Every Frame", &shadowMapsUpdateEveryFrame))
	{
		MarkShadowMapsDirty();
	}
	PushUndoIfItemActivated();

	if (ImGui::Checkbox("Update For Dynamic Props", &shadowMapsUpdateWhenDynamicPropsExist))
	{
		MarkShadowMapsDirty();
	}
	PushUndoIfItemActivated();

	if (ImGui::Checkbox("Update For Customers", &shadowMapsUpdateForCustomers))
	{
		MarkShadowMapsDirty();
	}
	PushUndoIfItemActivated();

	if (ImGui::Button("Rebuild Shadow Maps Now"))
	{
		MarkShadowMapsDirty();
	}
	if (ImGui::Button("Repair Scene Prop Shadow/Alpha Defaults"))
	{
		for (SceneProp& prop : sceneProps)
		{
			bool hasBlendTransparency = false;

			for (int mode : prop.materialAlphaModes)
			{
				if (mode == 2)
				{
					hasBlendTransparency = true;
					break;
				}
			}

			for (int mode : prop.meshAlphaModes)
			{
				if (mode == 2)
				{
					hasBlendTransparency = true;
					break;
				}
			}

			if (!prop.transparentMaterialIndices.empty())
				hasBlendTransparency = true;

			// Most props should be opaque by default.
			if (!hasBlendTransparency)
				prop.transparentAlpha = 255;

			if (prop.model != nullptr)
			{
				prop.castsShadow =
					ShouldScenePropCastShadowByName(prop.name) &&
					ShouldScenePropCastShadowByName(prop.sourceNodeName);
			}
		}

		MarkShadowMapsDirty();
	}
	ImGui::Text("Shadow Dirty: %s", shadowMapsDirty ? "Yes" : "No");
}
void Game::ResetScenePropColliderToModelBounds(SceneProp& prop)
{
	int propIndex = -1;

	if (!sceneProps.empty())
	{
		SceneProp* first = sceneProps.data();
		SceneProp* last = sceneProps.data() + sceneProps.size();

		if (&prop >= first && &prop < last)
		{
			propIndex = (int)(&prop - first);
		}
	}

	// Empty imported parent, such as PROP_xxx.
	if (prop.model == nullptr)
	{
		if (propIndex >= 0 && prop.importedFromGlbScene)
		{
			bool rebuilt = RebuildScenePropSubtreeCollider(propIndex);

			if (rebuilt)
			{
				prop.hasCollision = true;
				prop.blocksPlayer = true;
				prop.useJoltCollider = true;
				prop.useNormalCollision = false;
				prop.manualColliderOverride = true;

				MarkShadowMapsDirty();
				RecreatePhysicsWorld();
			}
		}

		return;
	}

	if (prop.importedFromGlbScene && prop.model != nullptr)
	{
		// Make runtime position/rotation/scale match the visual imported-editor transform
		// before calculating collider bounds. This prevents collider offset from
		// growing every time Reset Collider To Model Bounds is clicked.
		ApplyImportedEditorTransformToRuntime(prop);
	}

	BoundingBox localBounds{};

	if (!GetScenePropModelBoundsInColliderLocal(prop, localBounds))
		return;

	Vector3 size = {
		localBounds.max.x - localBounds.min.x,
		localBounds.max.y - localBounds.min.y,
		localBounds.max.z - localBounds.min.z
	};

	Vector3 center = {
		(localBounds.min.x + localBounds.max.x) * 0.5f,
		(localBounds.min.y + localBounds.max.y) * 0.5f,
		(localBounds.min.z + localBounds.max.z) * 0.5f
	};

	const float minSize = 0.01f;

	prop.colliderSize = {
		fmaxf(size.x, minSize),
		fmaxf(size.y, minSize),
		fmaxf(size.z, minSize)
	};

	prop.colliderOffset = center;

	prop.hasCollision = true;
	prop.blocksPlayer = true;
	prop.useJoltCollider = true;
	prop.useNormalCollision = false;
	prop.manualColliderOverride = true;

	if (prop.importedFromGlbScene && prop.model != nullptr)
	{
		ApplyScenePropTransformToBody(prop);
		SyncImportedEditorOffsetFromRuntime(prop);
	}

	MarkShadowMapsDirty();
	RecreatePhysicsWorld();
}
const char* Game::GetRenderResolutionModeName(RenderResolutionMode mode) const
{
	switch (mode)
	{
	case RenderResolutionMode::Native:  return "Native";
	case RenderResolutionMode::P1440:   return "1440p";
	case RenderResolutionMode::P1080:   return "1080p";
	case RenderResolutionMode::P720:    return "720p";
	case RenderResolutionMode::Scale75: return "75% Scale";
	case RenderResolutionMode::Scale50: return "50% Scale";
	default:                            return "Unknown";
	}
}

void Game::GetDesiredRenderTargetSize(int& outW, int& outH) const
{
	int screenW = GetScreenWidth();
	int screenH = GetScreenHeight();

	if (screenW <= 0 || screenH <= 0)
	{
		outW = 1280;
		outH = 720;
		return;
	}

	switch (renderResolutionMode)
	{
	case RenderResolutionMode::Native:
	{
		outW = screenW;
		outH = screenH;
	} break;

	case RenderResolutionMode::Scale75:
	{
		outW = (int)((float)screenW * 0.75f);
		outH = (int)((float)screenH * 0.75f);
	} break;

	case RenderResolutionMode::Scale50:
	{
		outW = (int)((float)screenW * 0.50f);
		outH = (int)((float)screenH * 0.50f);
	} break;

	case RenderResolutionMode::P1440:
	{
		outW = std::min(screenW, 2560);
		outH = std::min(screenH, 1440);
	} break;

	case RenderResolutionMode::P1080:
	{
		outW = std::min(screenW, 1920);
		outH = std::min(screenH, 1080);
	} break;

	case RenderResolutionMode::P720:
	{
		outW = std::min(screenW, 1280);
		outH = std::min(screenH, 720);
	} break;
	}

	outW = std::max(1, outW);
	outH = std::max(1, outH);
}

void Game::SetRenderResolutionMode(RenderResolutionMode mode)
{
	if (renderResolutionMode == mode)
		return;

	renderResolutionMode = mode;

	// Force recreation on next EnsureRenderTargetsMatchWindow().
	renderTargetWidth = 0;
	renderTargetHeight = 0;

	EnsureRenderTargetsMatchWindow();
}

bool Game::EnsureRenderTargetsMatchWindow()
{
	int screenW = GetScreenWidth();
	int screenH = GetScreenHeight();

	if (screenW <= 0 || screenH <= 0)
		return false;

	int w = 0;
	int h = 0;
	GetDesiredRenderTargetSize(w, h);

	int bloomW = std::max(1, w / std::max(1, bloomDownscale));
	int bloomH = std::max(1, h / std::max(1, bloomDownscale));

	bool needsRecreate =
		heldPropTarget.id == 0 ||
		worldTarget.id == 0 ||
		blurPingTarget.id == 0 ||
		blurPongTarget.id == 0 ||
		bloomExtractTarget.id == 0 ||
		bloomPingTarget.id == 0 ||
		bloomPongTarget.id == 0 ||

		renderTargetWidth != w ||
		renderTargetHeight != h ||

		heldPropTarget.texture.width != w ||
		heldPropTarget.texture.height != h ||

		worldTarget.texture.width != w ||
		worldTarget.texture.height != h ||

		blurPingTarget.texture.width != w ||
		blurPingTarget.texture.height != h ||

		blurPongTarget.texture.width != w ||
		blurPongTarget.texture.height != h ||

		bloomExtractTarget.texture.width != bloomW ||
		bloomExtractTarget.texture.height != bloomH ||

		bloomPingTarget.texture.width != bloomW ||
		bloomPingTarget.texture.height != bloomH ||

		bloomPongTarget.texture.width != bloomW ||
		bloomPongTarget.texture.height != bloomH;

	if (!needsRecreate)
		return false;

	TraceLog(
		LOG_WARNING,
		"Recreating render targets: mode=%s | internal=%i x %i | screen=%i x %i | bloom=%i x %i",
		GetRenderResolutionModeName(renderResolutionMode),
		w,
		h,
		screenW,
		screenH,
		bloomW,
		bloomH
	);

	if (heldPropTarget.id != 0) UnloadRenderTexture(heldPropTarget);
	if (worldTarget.id != 0) UnloadRenderTexture(worldTarget);
	if (blurPingTarget.id != 0) UnloadRenderTexture(blurPingTarget);
	if (blurPongTarget.id != 0) UnloadRenderTexture(blurPongTarget);

	if (bloomExtractTarget.id != 0) UnloadRenderTexture(bloomExtractTarget);
	if (bloomPingTarget.id != 0) UnloadRenderTexture(bloomPingTarget);
	if (bloomPongTarget.id != 0) UnloadRenderTexture(bloomPongTarget);

	heldPropTarget = LoadRenderTexture(w, h);
	worldTarget = LoadRenderTexture(w, h);
	blurPingTarget = LoadRenderTexture(w, h);
	blurPongTarget = LoadRenderTexture(w, h);

	bloomExtractTarget = LoadRenderTexture(bloomW, bloomH);
	bloomPingTarget = LoadRenderTexture(bloomW, bloomH);
	bloomPongTarget = LoadRenderTexture(bloomW, bloomH);

	// Optional: reduces blocky upscaling when using 75%, 50%, 1080p, or 720p.
	SetTextureFilter(worldTarget.texture, TEXTURE_FILTER_BILINEAR);
	SetTextureFilter(heldPropTarget.texture, TEXTURE_FILTER_BILINEAR);
	SetTextureFilter(blurPingTarget.texture, TEXTURE_FILTER_BILINEAR);
	SetTextureFilter(blurPongTarget.texture, TEXTURE_FILTER_BILINEAR);
	SetTextureFilter(bloomExtractTarget.texture, TEXTURE_FILTER_BILINEAR);
	SetTextureFilter(bloomPingTarget.texture, TEXTURE_FILTER_BILINEAR);
	SetTextureFilter(bloomPongTarget.texture, TEXTURE_FILTER_BILINEAR);

	renderTargetWidth = w;
	renderTargetHeight = h;

	ClearRenderTargetsOnce();

	inspectTransitionFrames = 3;

	return true;
}

void Game::BuildCustomerBodies()
{
	RemoveCustomerBodies();

	if (!physics)
		return;

	customerBodyIds.clear();
	customerBodyIds.reserve(customers.size());

	for (Customer& customer : customers)
	{
		Vector3 center = GetCustomerColliderCenter(customer);

		Vector3 halfExtents = {
			0.225f,
			0.875f,
			0.225f
		};

		Quaternion rotation = GetCustomerColliderRotation(customer);

		JPH::BodyID bodyId = physics->AddKinematicBox(
			center,
			halfExtents,
			rotation
		);

		if (!bodyId.IsInvalid())
		{
			// Customers should not physically push or be pushed by player / held props.
			// Their movement is controlled by Customer.position + AI/pathing.
			physics->SetBodyIsSensor(bodyId, true);
			physics->SetBodyGravityFactor(bodyId, 0.0f);
			physics->SetBodyLinearVelocity(bodyId, { 0.0f, 0.0f, 0.0f });
			physics->SetBodyAngularVelocity(bodyId, { 0.0f, 0.0f, 0.0f });
		}

		customerBodyIds.push_back(bodyId);
	}

	TraceLog(
		LOG_INFO,
		"Built %i customer kinematic bodies.",
		(int)customerBodyIds.size()
	);
}

void Game::SyncCustomerBodiesToCustomers(float dt)
{
	if (!physics)
		return;

	int count = std::min(
		(int)customers.size(),
		(int)customerBodyIds.size()
	);

	for (int i = 0; i < count; i++)
	{
		JPH::BodyID bodyId = customerBodyIds[i];

		if (bodyId.IsInvalid())
			continue;

		Customer& customer = customers[i];

		Vector3 center = GetCustomerColliderCenter(customer);
		Quaternion rotation = GetCustomerColliderRotation(customer);

		physics->MoveKinematicBody(
			bodyId,
			center,
			rotation,
			dt
		);
	}
}

void Game::RemoveCustomerBodies()
{
	if (!physics)
	{
		customerBodyIds.clear();
		return;
	}

	for (JPH::BodyID bodyId : customerBodyIds)
	{
		if (!bodyId.IsInvalid())
		{
			physics->RemoveBody(bodyId);
		}
	}

	customerBodyIds.clear();
}

void Game::ClearRenderTargetsOnce()
{
	if (heldPropTarget.id != 0)
	{
		BeginTextureMode(heldPropTarget);
		ClearBackground(BLANK);
		EndTextureMode();
	}

	if (worldTarget.id != 0)
	{
		BeginTextureMode(worldTarget);
		ClearBackground(BLACK);
		EndTextureMode();
	}

	if (blurPingTarget.id != 0)
	{
		BeginTextureMode(blurPingTarget);
		ClearBackground(BLACK);
		EndTextureMode();
	}

	if (blurPongTarget.id != 0)
	{
		BeginTextureMode(blurPongTarget);
		ClearBackground(BLACK);
		EndTextureMode();
	}
}

Vector3 Game::GetScaledColliderOffset(const SceneProp& prop) const
{
	return {
		prop.colliderOffset.x * prop.scale.x,
		prop.colliderOffset.y * prop.scale.y,
		prop.colliderOffset.z * prop.scale.z
	};
}

Vector3 Game::GetScenePropRotatedOffset(const SceneProp& prop) const
{
	Quaternion q = QuaternionFromEuler(
		prop.rotationDeg.x * DEG2RAD,
		prop.rotationDeg.y * DEG2RAD,
		prop.rotationDeg.z * DEG2RAD
	);

	Vector3 scaledOffset = GetScaledColliderOffset(prop);
	return Vector3RotateByQuaternion(scaledOffset, q);
}
void Game::BuildOutlineModelCache(const Model* model)
{
	if (model == nullptr)
		return;

	if (GetOutlineModelCache(model) != nullptr)
		return;

	OutlineModelCache cache;
	cache.source = model;

	for (int i = 0; i < model->meshCount; i++)
	{
		cache.meshes.push_back(CloneMeshForOutline(model->meshes[i]));
	}

	outlineModelCaches.push_back(std::move(cache));
}

void Game::ApplyScenePropTransformToBody(SceneProp& prop)
{
	if (prop.bodyId.IsInvalid()) return;

	Quaternion q = QuaternionFromEuler(
		prop.rotationDeg.x * DEG2RAD,
		prop.rotationDeg.y * DEG2RAD,
		prop.rotationDeg.z * DEG2RAD
	);

	Vector3 scaledOffset = GetScaledColliderOffset(prop);
	Vector3 rotatedOffset = Vector3RotateByQuaternion(scaledOffset, q);
	Vector3 center = Vector3Add(prop.position, rotatedOffset);

	physics->SetBodyPosition(prop.bodyId, center);
	physics->SetBodyRotation(prop.bodyId, q);
	physics->SetBodyLinearVelocity(prop.bodyId, { 0.0f, 0.0f, 0.0f });
	physics->SetBodyAngularVelocity(prop.bodyId, { 0.0f, 0.0f, 0.0f });
}

void Game::ReadScenePropTransformFromBody(SceneProp& prop)
{
	if (prop.bodyId.IsInvalid()) return;

	Vector3 center = physics->GetBodyPosition(prop.bodyId);
	Quaternion q = physics->GetBodyRotation(prop.bodyId);

	Vector3 rotDeg = QuaternionToEuler(q);
	rotDeg.x *= RAD2DEG;
	rotDeg.y *= RAD2DEG;
	rotDeg.z *= RAD2DEG;

	prop.rotationDeg = rotDeg;

	Vector3 scaledOffset = GetScaledColliderOffset(prop);
	Vector3 rotatedOffset = Vector3RotateByQuaternion(scaledOffset, q);
	prop.position = Vector3Subtract(center, rotatedOffset);
}
Game::~Game()
{

	RemoveCustomerBodies();

	if (heldPropTarget.id != 0)
	{
		UnloadRenderTexture(heldPropTarget);
		heldPropTarget.id = 0;
	}
	if (worldTarget.id != 0) UnloadRenderTexture(worldTarget);
	if (blurPingTarget.id != 0) UnloadRenderTexture(blurPingTarget);
	if (blurPongTarget.id != 0) UnloadRenderTexture(blurPongTarget);
	if (blurShader.id != 0) UnloadShader(blurShader);

	UnloadOutlineModelCaches();
	if (outlineShader.id != 0)
	{
		UnloadShader(outlineShader);
		outlineShader.id = 0;
	}

	for (auto& modelPtr : customerModels)
	{
		if (modelPtr && modelPtr->meshCount > 0)
		{
			UnloadModel(*modelPtr);
		}
	}

	customerModels.clear();

	for (auto& pair : customerTypes)
	{
		CustomerType& type = pair.second;

		if (type.animSet.animations != nullptr)
		{
			UnloadModelAnimations(
				type.animSet.animations,
				type.animSet.animationCount
			);

			type.animSet.animations = nullptr;
			type.animSet.animationCount = 0;
		}
	}

	if (shadowDepthAnimatedShader.id != 0)
	{
		UnloadShader(shadowDepthAnimatedShader);
		shadowDepthAnimatedShader.id = 0;
	}

	customerTypes.clear();

	if (customerModel.meshCount > 0)
	{
		UnloadModel(customerModel);
	}
	if (neutralNormalTexture.id != 0) UnloadTexture(neutralNormalTexture);
	if (defaultGltfMRTexture.id != 0) UnloadTexture(defaultGltfMRTexture);

	if (instancedPbrShader.id != 0)
	{
		UnloadShader(instancedPbrShader);
		instancedPbrShader = {};
	}

	if (bloomExtractTarget.id != 0) UnloadRenderTexture(bloomExtractTarget);
	if (bloomPingTarget.id != 0) UnloadRenderTexture(bloomPingTarget);
	if (bloomPongTarget.id != 0) UnloadRenderTexture(bloomPongTarget);

	if (brightExtractShader.id != 0) UnloadShader(brightExtractShader);

	if (uiFontLoaded)
	{
		UnloadFont(uiFont);
		uiFontLoaded = false;
	}

	UnloadSoundEffects();
	UnloadRecordPlayerTrack();
	UnloadRecordPlayerCoverImage();
	UnloadJapanStoreStatic();
	UnloadGachaMachineInstanceTest();
	UnloadGachaBallInstances();
	UnloadBasketInstances();
	UnloadMainMenuMusic();

	UnloadScenePropOcclusionQueries();
	UnloadSkybox();
	rlImGuiShutdown();
	CloseAudioDevice();
	CloseWindow();
}

void Game::run()
{
	while (!WindowShouldClose() && !requestQuit)
	{
		update();
		draw();
	}
}

void Game::draw()
{
	double drawStart = GetTime();

	if (currentScreen == GameScreen::Loading)
	{
		DrawLoadingScreen();
		return;
	}

	if (currentScreen == GameScreen::MainMenu)
	{
		BeginDrawing();
		ClearBackground(Color{ 18, 20, 24, 255 });

		DrawMainMenu();

		EndDrawing();
		return;
	}

	if (currentScreen == GameScreen::Controls)
	{
		BeginDrawing();
		ClearBackground(Color{ 18, 20, 24, 255 });

		DrawControlsMenu();

		EndDrawing();
		return;
	}

	if (currentScreen == GameScreen::AudioSettings)
	{
		BeginDrawing();
		ClearBackground(Color{ 18, 20, 24, 255 });

		DrawAudioSettingsMenu();

		EndDrawing();
		return;
	}

	if (currentScreen == GameScreen::GraphicsSettings)
	{
		BeginDrawing();
		ClearBackground(Color{ 18, 20, 24, 255 });

		DrawGraphicsSettingsMenu();

		EndDrawing();
		return;
	}

	if (currentScreen == GameScreen::Credits)
	{
		BeginDrawing();
		ClearBackground(Color{ 18, 20, 24, 255 });

		DrawCreditsMenu();

		EndDrawing();
		return;
	}

	const bool showEditorUI = editMode || (cursorUnlocked && !inspectMode);

	if (EnsureRenderTargetsMatchWindow())
	{
		RefreshDialogueUILayout();
	}

	Camera3D activeCamera = editMode ? editorCamera : camera;

	double t = GetTime();
	// Render held prop separately first
	if (!editMode && hasHeldBody)
	{
		DrawHeldPropToTarget(activeCamera);
	}

	perf.heldPropRenderMs = MsSince(t);
	t = GetTime();
	RenderShadowMapsIfNeeded();
	perf.shadowMs = MsSince(t);

	// Render the world/background to its own target
	t = GetTime();

	DrawWorldToTarget(activeCamera);
	perf.worldRenderMs = MsSince(t);
	t = GetTime();
	RenderTexture2D* bloomResult = RenderBloomToTarget();
	perf.bloomRenderMs = MsSince(t);

	t = GetTime();
	BeginDrawing();
	ClearBackground(WHITE);
	perf.beginDrawingMs = MsSince(t);

	Rectangle src = {
		0.0f,
		0.0f,
		(float)worldTarget.texture.width,
		-(float)worldTarget.texture.height
	};

	Rectangle dst = {
		0.0f,
		0.0f,
		(float)GetScreenWidth(),
		(float)GetScreenHeight()
	};



	bool allowInspectBlur =
		!editMode &&
		inspectMode &&
		inspectTransitionFrames <= 0;

	if (allowInspectBlur)
	{
		DrawBlurredWorldToScreen();
	}
	else
	{
		DrawTexturePro(worldTarget.texture, src, dst, { 0.0f, 0.0f }, 0.0f, WHITE);
	}
	// Bloom must be drawn AFTER the world.
	if (bloomEnabled && bloomResult != nullptr)
	{
		Rectangle bloomSrc = {
			0.0f,
			0.0f,
			(float)bloomResult->texture.width,
			-(float)bloomResult->texture.height
		};

		Rectangle screenDst = {
			0.0f,
			0.0f,
			(float)GetScreenWidth(),
			(float)GetScreenHeight()
		};

		unsigned char alpha = (unsigned char)Clamp(
			bloomIntensity * 255.0f,
			0.0f,
			255.0f
		);

		BeginBlendMode(BLEND_ADDITIVE);

		DrawTexturePro(
			bloomResult->texture,
			bloomSrc,
			screenDst,
			{ 0.0f, 0.0f },
			0.0f,
			Color{ 255, 255, 255, alpha }
		);

		EndBlendMode();
	}

	float blackoutAmount = GetMusicBoxSceneBlackoutAmount();

	if (blackoutAmount > 0.001f)
	{
		unsigned char overlayAlpha = (unsigned char)(185.0f * blackoutAmount);

		BeginBlendMode(BLEND_ALPHA);

		DrawRectangle(
			0,
			0,
			GetScreenWidth(),
			GetScreenHeight(),
			Color{ 0, 0, 0, overlayAlpha }
		);

		EndBlendMode();
	}

	/*
	//debug shadows
		if (shadowCasters[0].shadowMap.texture.id != 0)
	{
		Rectangle shadowSrc = {
			0.0f,
			0.0f,
			(float)shadowCasters[0].shadowMap.texture.width,
			-(float)shadowCasters[0].shadowMap.texture.height
		};

		Rectangle shadowDst = {
			20.0f,
			20.0f,
			256.0f,
			256.0f
		};

		DrawTexturePro(
			shadowCasters[0].shadowMap.texture,
			shadowSrc,
			shadowDst,
			{ 0.0f, 0.0f },
			0.0f,
			WHITE
		);
	}
	*/


	// Composite held prop on top of world, below UI
	if (!editMode && hasHeldBody && heldPropTarget.id != 0 && inspectTransitionFrames <= 0)
	{
		Rectangle heldSrc = {
			0.0f,
			0.0f,
			(float)heldPropTarget.texture.width,
			-(float)heldPropTarget.texture.height
		};

		Rectangle heldDst = {
			0.0f,
			0.0f,
			(float)GetScreenWidth(),
			(float)GetScreenHeight()
		};

		BeginBlendMode(BLEND_ALPHA);
		DrawTexturePro(heldPropTarget.texture, heldSrc, heldDst, { 0.0f, 0.0f }, 0.0f, WHITE);
		EndBlendMode();
	}

	perf.screenCompositeMs = MsSince(t);

	// Draw dialogue UI after the 3D world and held prop,
	// but before ImGui editor UI.
	t = GetTime();

	DrawDialogue();
	DrawBudgetHUD();
	DrawStoreStatusHUD();

	DrawRecordPlayerTrackOverlay();

	DrawFinalScoreOverlay();
	DrawDayIntroOverlay();
	DrawCampaignCompleteScreen();

	perf.dialogueDrawMs = MsSince(t);

	if (currentScreen == GameScreen::Paused)
	{
		DrawPauseMenu();
	}

	t = GetTime();
	if (showEditorUI)
	{
		rlImGuiBegin();
		UpdateEditorScenePicking(activeCamera);
		DrawSceneEditorUI();
		rlImGuiEnd();
	}

	perf.editorUIMs = MsSince(t);

	if (inspectTransitionFrames > 0)
		inspectTransitionFrames--;

	t = GetTime();
	DrawPerformanceOverlay();
	perf.performanceOverlayMs = MsSince(t);

	t = GetTime();
	EndDrawing();
	perf.endDrawingMs = MsSince(t);

	perf.drawMs = MsSince(drawStart);

	perf.drawKnownPartsMs =
		perf.heldPropRenderMs +
		perf.shadowMs +
		perf.worldRenderMs +
		perf.bloomRenderMs +
		perf.beginDrawingMs +
		perf.screenCompositeMs +
		perf.dialogueDrawMs +
		perf.editorUIMs +
		perf.performanceOverlayMs +
		perf.endDrawingMs;

	perf.drawUnaccountedMs = perf.drawMs - perf.drawKnownPartsMs;
}

Game::StoreDayConfig Game::GetStoreDayConfig(int day) const
{
	day = Clamp(day, 1, maxStoreDays);

	switch (day)
	{
	case 1:
		return StoreDayConfig{
			1,
			5,
			2500,   // buyer sales target
			1500,   // seller buy target
			12.0f,
			18.0f,
			30
		};

	case 2:
		return StoreDayConfig{
			2,
			7,
			5000,
			3000,
			10.0f,
			15.0f,
			35
		};

	case 3:
		return StoreDayConfig{
			3,
			10,
			8000,
			5500,
			8.0f,
			12.0f,
			40
		};

	case 4:
		return StoreDayConfig{
			4,
			12,
			11000,
			8000,
			6.5f,
			10.0f,
			45
		};

	case 5:
	default:
		return StoreDayConfig{
			5,
			15,
			15000,
			11000,
			5.0f,
			8.0f,
			50
		};
	}
}

Game::StoreDayConfig Game::GetCurrentStoreDayConfig() const
{
	return GetStoreDayConfig(currentStoreDay);
}

float Game::GetCurrentDaySpawnDelay() const
{
	StoreDayConfig cfg = GetCurrentStoreDayConfig();

	return (float)GetRandomValue(
		(int)(cfg.spawnDelayMin * 100.0f),
		(int)(cfg.spawnDelayMax * 100.0f)
	) / 100.0f;
}

void Game::CaptureFinalDayResult()
{
	StoreDayConfig cfg = GetCurrentStoreDayConfig();

	finalDayResult.valid = true;

	finalDayResult.reason =
		closeShopReason.empty()
		? "Day complete."
		: closeShopReason;

	finalDayResult.day = currentStoreDay;

	finalDayResult.customersSpawned = customersSpawnedToday;
	finalDayResult.customersTarget = cfg.customerTarget;
	finalDayResult.customersCompleted = customersCompletedToday;

	finalDayResult.startingBudgetYen = storeBudgetAtOpen;
	finalDayResult.endingBudgetYen = storeBudgetYen;

	finalDayResult.profitYen =
		finalDayResult.endingBudgetYen -
		finalDayResult.startingBudgetYen;

	finalDayResult.buyerSalesYen = buyerSalesToday;
	finalDayResult.buyerSalesTargetYen = cfg.minBuyerSalesYen;

	finalDayResult.sellerBuySpendYen = sellerBuySpendToday;
	finalDayResult.sellerBuyTargetYen = cfg.minSellerBuyYen;
	finalDayResult.sellerPurchases = sellerPurchasesToday;

	finalDayResult.customerBonus =
		finalDayResult.customersCompleted * 100;

	finalDayResult.finalScore =
		finalDayResult.profitYen +
		finalDayResult.customerBonus +
		finalDayResult.buyerSalesYen / 10;

	finalDayResult.passed = DidPassCurrentDay();

	finalDayResult.campaignComplete =
		finalDayResult.passed &&
		currentStoreDay >= maxStoreDays;
}

void Game::ShowCampaignCompleteScreen(bool debugTriggered)
{
	campaignComplete = true;
	campaignCompleteScreenVisible = true;
	campaignCompleteDebugTriggered = debugTriggered;

	finalScoreVisible = false;
	dayIntroActive = false;

	customerSpawningEnabled = false;

	if (hasHeldBody)
	{
		StopHoldingBody();
	}

	StopPlayerMovementForDialogue();

	TraceLog(
		LOG_INFO,
		"Campaign complete screen shown. debug=%i",
		debugTriggered ? 1 : 0
	);
}

void Game::UpdateCampaignCompleteScreen()
{
	if (!campaignCompleteScreenVisible)
		return;

	StopPlayerMovementForDialogue();

	if (IsKeyPressed(KEY_ENTER) ||
		IsKeyPressed(KEY_SPACE) ||
		IsKeyPressed(KEY_ESCAPE))
	{
		campaignCompleteScreenVisible = false;
		ReturnToMainMenu();
	}
}

void Game::DrawCampaignCompleteScreen() const
{
	if (!campaignCompleteScreenVisible)
		return;

	int screenW = GetScreenWidth();
	int screenH = GetScreenHeight();

	DrawRectangle(
		0,
		0,
		screenW,
		screenH,
		Color{ 0, 0, 0, 215 }
	);

	Font font = uiFontLoaded ? uiFont : GetFontDefault();

	const char* title = "CONGRATULATIONS!";
	const char* subtitle = "You kept Gemmu Shoppu running for 5 days.";
	const char* thanks = "Thanks for playing.";
	const char* prompt = "Press Enter / Space to return to main menu";

	if (campaignCompleteDebugTriggered)
	{
		subtitle = "Debug campaign-complete preview.";
	}

	Vector2 titleSize = MeasureTextEx(font, title, 58.0f, 2.0f);
	Vector2 subtitleSize = MeasureTextEx(font, subtitle, 24.0f, 1.0f);
	Vector2 thanksSize = MeasureTextEx(font, thanks, 34.0f, 1.0f);
	Vector2 promptSize = MeasureTextEx(font, prompt, 18.0f, 1.0f);

	float centerY = screenH * 0.5f;

	DrawTextEx(
		font,
		title,
		Vector2{
			screenW * 0.5f - titleSize.x * 0.5f,
			centerY - 110.0f
		},
		58.0f,
		2.0f,
		Color{ 120, 255, 150, 255 }
	);

	DrawTextEx(
		font,
		subtitle,
		Vector2{
			screenW * 0.5f - subtitleSize.x * 0.5f,
			centerY - 32.0f
		},
		24.0f,
		1.0f,
		Color{ 220, 225, 235, 255 }
	);

	DrawTextEx(
		font,
		thanks,
		Vector2{
			screenW * 0.5f - thanksSize.x * 0.5f,
			centerY + 24.0f
		},
		34.0f,
		1.0f,
		WHITE
	);

	DrawTextEx(
		font,
		prompt,
		Vector2{
			screenW * 0.5f - promptSize.x * 0.5f,
			centerY + 112.0f
		},
		18.0f,
		1.0f,
		Color{ 180, 190, 210, 255 }
	);
}

int Game::CalculateFinalScore() const
{
	if (finalDayResult.valid)
		return finalDayResult.finalScore;

	int profit = storeBudgetYen - storeBudgetAtOpen;

	int score =
		profit +
		customersCompletedToday * 100;

	return score;
}

void Game::DrawFinalScoreOverlay() const
{
	if (!finalScoreVisible)
		return;

	int screenW = GetScreenWidth();
	int screenH = GetScreenHeight();

	DrawRectangle(
		0,
		0,
		screenW,
		screenH,
		Color{ 0, 0, 0, 190 }
	);

	Font font = uiFontLoaded ? uiFont : GetFontDefault();

	StoreDayResult result = finalDayResult;

	if (!result.valid)
	{
		result.reason =
			closeShopReason.empty()
			? "Day complete."
			: closeShopReason;

		result.day = currentStoreDay;
		result.customersSpawned = customersSpawnedToday;
		result.customersTarget = customersToProcessBeforeClose;
		result.customersCompleted = customersCompletedToday;

		result.startingBudgetYen = storeBudgetAtOpen;
		result.endingBudgetYen = storeBudgetYen;
		result.profitYen = storeBudgetYen - storeBudgetAtOpen;

		result.buyerSalesYen = buyerSalesToday;
		result.sellerBuySpendYen = sellerBuySpendToday;

		StoreDayConfig cfg = GetCurrentStoreDayConfig();
		result.buyerSalesTargetYen = cfg.minBuyerSalesYen;
		result.sellerBuyTargetYen = cfg.minSellerBuyYen;

		result.customerBonus = customersCompletedToday * 100;
		result.finalScore = result.profitYen + result.customerBonus;
		result.passed = DidPassCurrentDay();
		result.campaignComplete =
			result.passed && currentStoreDay >= maxStoreDays;
	}

	float panelW = 680.0f;
	float panelH = 500.0f;

	Rectangle panel{
		(screenW - panelW) * 0.5f,
		(screenH - panelH) * 0.5f,
		panelW,
		panelH
	};

	DrawRectangleRounded(
		panel,
		0.06f,
		12,
		Color{ 25, 25, 32, 245 }
	);

	DrawRectangleRoundedLines(
		panel,
		0.06f,
		12,
		Color{ 255, 255, 255, 90 }
	);

	float x = panel.x + 44.0f;
	float y = panel.y + 34.0f;

	Color passColor = result.passed
		? Color{ 120, 255, 150, 255 }
	: Color{ 255, 160, 120, 255 };

	DrawTextEx(
		font,
		TextFormat(
			"Day %i Result - %s",
			result.day,
			result.passed ? "Passed" : "Quota Missed"
		),
		Vector2{ x, y },
		32.0f,
		1.0f,
		passColor
	);

	y += 52.0f;

	DrawTextEx(
		font,
		TextFormat(
			"Reason: %s",
			result.reason.empty() ? "Day complete." : result.reason.c_str()
		),
		Vector2{ x, y },
		18.0f,
		1.0f,
		Color{ 210, 210, 220, 255 }
	);

	y += 42.0f;

	auto DrawResultLine = [&](const char* label, const char* value, Color valueColor)
		{
			DrawTextEx(
				font,
				label,
				Vector2{ x, y },
				20.0f,
				1.0f,
				Color{ 210, 210, 220, 255 }
			);

			DrawTextEx(
				font,
				value,
				Vector2{ x + 270.0f, y },
				20.0f,
				1.0f,
				valueColor
			);

			y += 29.0f;
		};

	DrawResultLine(
		"Customers spawned",
		TextFormat("%i / %i", result.customersSpawned, result.customersTarget),
		result.customersSpawned >= result.customersTarget ? passColor : Color{ 255, 160, 120, 255 }
	);

	DrawResultLine(
		"Customers completed",
		TextFormat("%i", result.customersCompleted),
		result.customersCompleted >= result.customersTarget ? passColor : Color{ 255, 160, 120, 255 }
	);

	DrawResultLine(
		"Buyer sales",
		TextFormat("\xC2\xA5%i / \xC2\xA5%i", result.buyerSalesYen, result.buyerSalesTargetYen),
		result.buyerSalesYen >= result.buyerSalesTargetYen ? passColor : Color{ 255, 160, 120, 255 }
	);

	DrawResultLine(
		"Seller buys",
		TextFormat("\xC2\xA5%i / \xC2\xA5%i", result.sellerBuySpendYen, result.sellerBuyTargetYen),
		result.sellerBuySpendYen >= result.sellerBuyTargetYen ? passColor : Color{ 255, 160, 120, 255 }
	);

	y += 8.0f;

	DrawResultLine(
		"Starting budget",
		TextFormat("\xC2\xA5%i", result.startingBudgetYen),
		WHITE
	);

	DrawResultLine(
		"Ending budget",
		TextFormat("\xC2\xA5%i", result.endingBudgetYen),
		WHITE
	);

	DrawResultLine(
		"Net cash change",
		TextFormat("%s\xC2\xA5%i", result.profitYen >= 0 ? "+" : "-", abs(result.profitYen)),
		result.profitYen >= 0 ? Color{ 120, 255, 150, 255 } : Color{ 255, 160, 120, 255 }
	);

	DrawResultLine(
		"Customer bonus",
		TextFormat("+%i", result.customerBonus),
		Color{ 170, 210, 255, 255 }
	);

	// Final score gets its own safe area above the footer.
	float scoreY = panel.y + panel.height - 96.0f;

	DrawTextEx(
		font,
		TextFormat("Final Score: %i", result.finalScore),
		Vector2{ x, scoreY },
		28.0f,
		1.0f,
		YELLOW
	);

	const char* footerText = result.passed
		? (result.campaignComplete
			? "Campaign complete. Returning to menu..."
			: "Next day starting soon...")
		: "Quota missed. Retrying this day soon...";

	DrawTextEx(
		font,
		footerText,
		Vector2{ x, panel.y + panel.height - 42.0f },
		18.0f,
		1.0f,
		Color{ 190, 190, 200, 255 }
	);
}

void Game::UnloadJapanStoreStatic()
{
	if (japanStoreStaticLoaded)
	{
		UnloadModel(japanStoreStaticModel);
		japanStoreStaticModel = {};
		japanStoreStaticLoaded = false;
	}
}


void Game::DrawPerformanceOverlay()
{
	if (!showPerfOverlay)
		return;

	int x = 16;
	int y = 16;
	int line = 18;

	float frameMs = GetFrameTime() * 1000.0f;
	int fps = GetFPS();

	DrawRectangle(x - 8, y - 8, 360, 310, Color{ 0, 0, 0, 180 });

	DrawText(TextFormat("FPS: %i | Frame: %.2f ms", fps, frameMs), x, y, 18, LIME);
	y += line * 2;

	DrawText(TextFormat("Update Total: %.2f ms", perf.updateMs), x, y, 16, RAYWHITE);
	y += line;

	DrawText(TextFormat("  Customer AI/Nav: %.2f ms", perf.customerAIUpdateMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Customer Body Sync: %.2f ms", perf.customerBodySyncMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Physics Step: %.2f ms", perf.physicsMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Scene Physics Sync: %.2f ms", perf.scenePhysicsSyncMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Interact Target: %.2f ms", perf.interactTargetMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Dialogue Update: %.2f ms", perf.dialogueUpdateMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  PBR Shader Update: %.2f ms", perf.pbrUpdateMs), x, y, 16, LIGHTGRAY);
	y += line * 2;

	DrawText(TextFormat("Draw Total: %.2f ms", perf.drawMs), x, y, 16, RAYWHITE);
	y += line;

	DrawText(TextFormat("  Held Prop Render: %.2f ms", perf.heldPropRenderMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Shadow Render: %.2f ms", perf.shadowMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  World Render: %.2f ms", perf.worldRenderMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Bloom Render: %.2f ms", perf.bloomRenderMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Shadow Uniforms: %.2f ms", perf.shadowUniformUploadMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Shadow Attach: %.2f ms", perf.shadowTextureAttachMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Occlusion Culled: %i",
		perf.scenePropsOcclusionCulled
	), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Occluders: %i",
		(int)screenOccluders.size()
	), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Occluders: %i", (int)screenOccluders.size()), x, y, 16, LIGHTGRAY);
	y += line;


	DrawText(TextFormat("  GPU Occ Queries: %i", perf.gpuOcclusionQueriesIssued), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  GPU Occ Culled: %i", perf.gpuOcclusionCulled), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  GPU Occ Query: %.2f ms", perf.gpuOcclusionQueryMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  GPU Reject ShouldTest: %i", debugGpuOccRejectShouldTest), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  GPU Reject NoQueryArray: %i", debugGpuOccRejectNoQueryArray), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  GPU Reject Pending: %i", debugGpuOccRejectPending), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  GPU Reject NoBounds: %i", debugGpuOccRejectNoBounds), x, y, 16, LIGHTGRAY);
	y += line;

	// Add it here
	DrawText(TextFormat("  Scene Props: %i drawn / %i culled",
		perf.scenePropsDrawn,
		perf.scenePropsCulled
	), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Scene Prop Cull: %.2f ms", perf.scenePropCullMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Scene Prop Draw: %.2f ms", perf.scenePropDrawMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Screen Composite: %.2f ms", perf.screenCompositeMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Dialogue Draw: %.2f ms", perf.dialogueDrawMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Editor UI: %.2f ms", perf.editorUIMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Perf Overlay: %.2f ms", perf.performanceOverlayMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Physics Debug Draw: %.2f ms", perf.drawPhysicsDebugMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Begin Drawing: %.2f ms", perf.beginDrawingMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  End Drawing/Present: %.2f ms", perf.endDrawingMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  World Begin Target: %.2f ms", perf.worldTargetBeginMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  World End Target: %.2f ms", perf.worldTargetEndMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Draw Level/Floor: %.2f ms", perf.drawLevelMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Draw Blockout: %.2f ms", perf.drawBlockoutMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Draw Gacha Inst: %.2f ms", perf.drawGachaInstancedMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Draw Gacha Balls: %.2f ms", perf.drawGachaBallsMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Draw Gacha Machines: %.2f ms", perf.drawGachaMachinesMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Draw Basket Inst: %.2f ms", perf.drawBasketInstancesMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  World Known: %.2f ms", perf.worldKnownPartsMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  World Unaccounted: %.2f ms", perf.worldUnaccountedMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Draw Known: %.2f ms", perf.drawKnownPartsMs), x, y, 16, LIGHTGRAY);
	y += line;

	DrawText(TextFormat("  Draw Unaccounted: %.2f ms", perf.drawUnaccountedMs), x, y, 16, LIGHTGRAY);
	y += line;
}


bool Game::DrawUISlider(Rectangle rect, const char* label, float& value)
{
	value = Clamp(value, 0.0f, 1.0f);

	DrawText(label, (int)rect.x, (int)rect.y - 28, 20, WHITE);

	DrawRectangleRounded(rect, 0.4f, 8, Color{ 40, 45, 55, 255 });

	Rectangle fill = rect;
	fill.width *= value;

	DrawRectangleRounded(fill, 0.4f, 8, Color{ 80, 150, 220, 255 });

	float knobX = rect.x + rect.width * value;

	DrawCircle(
		(int)knobX,
		(int)(rect.y + rect.height * 0.5f),
		12.0f,
		WHITE
	);

	bool changed = false;
	Vector2 mouse = GetMousePosition();

	Rectangle hitArea = {
		rect.x - 10.0f,
		rect.y - 10.0f,
		rect.width + 20.0f,
		rect.height + 20.0f
	};

	if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) &&
		CheckCollisionPointRec(mouse, hitArea))
	{
		value = Clamp((mouse.x - rect.x) / rect.width, 0.0f, 1.0f);
		changed = true;
	}

	DrawText(
		TextFormat("%i%%", (int)(value * 100.0f)),
		(int)(rect.x + rect.width + 20),
		(int)(rect.y - 4),
		20,
		WHITE
	);

	return changed;
}

int Game::GetLoadingStepCount() const
{
	return 17;
}

const char* Game::GetLoadingStepName(int step) const
{
	switch (step)
	{
	case 0:  return "Creating physics and render targets...";
	case 1:  return "Creating player and camera...";
	case 2:  return "Loading base assets, textures, models, and shaders...";
	case 3:  return "Setting up lighting...";
	case 4:  return "Loading gacha machine assets...";
	case 5:  return "Loading basket and cartridge assets...";
	case 6:  return "Creating instanced props...";
	case 7:  return "Creating test scene...";
	case 8:  return "Loading customer types and model pool...";
	case 9:  return "Creating customers...";
	case 10: return "Building scene props...";
	case 11: return "Building blockout and placement spots...";
	case 12: return "Building dialogue...";
	case 13: return "Loading saved scene...";
	case 14: return "Building physics colliders...";
	case 15: return "Creating player controller...";
	case 16: return "Finishing setup...";
	default: return "Ready.";
	}
}
void Game::UpdateLoading()
{
	const int totalSteps = GetLoadingStepCount();

	if (loadingStep >= totalSteps)
	{
		FinishLoading();
		return;
	}

	// Phase A:
	// Show the next loading message for one frame before doing the heavy work.
	if (!loadingStepPrimed)
	{
		loadingStatus = GetLoadingStepName(loadingStep);
		loadingProgress = (float)loadingStep / (float)totalSteps;
		loadingStepPrimed = true;
		return;
	}

	// Phase B:
	// Actually perform this loading step.
	switch (loadingStep)
	{
	case 0:
	{
		physics = std::make_unique<PhysicsWorld>();

		DisableCursor();
		cursorCapturedByGame = true;
		cursorUnlocked = false;

		heldPropTarget = {};
		worldTarget = {};
		blurPingTarget = {};
		blurPongTarget = {};

		EnsureRenderTargetsMatchWindow();

		blurShader = LoadShader(0, "Shaders/blur.fs");
		blurDirectionLoc = GetShaderLocation(blurShader, "direction");
		blurTexelSizeLoc = GetShaderLocation(blurShader, "texelSize");

		brightExtractShader = LoadShader(0, "Shaders/bright_extract.fs");
		brightThresholdLoc = GetShaderLocation(brightExtractShader, "threshold");
		brightKneeLoc = GetShaderLocation(brightExtractShader, "knee");

	} break;

	case 1:
	{
		Model playerModel = LoadModelFromMesh(GenMeshCube(0.001f, 0.001f, 0.001f));
		player = Player(playerModel, playerSpawnPosition, nullptr);

		camera.up = Vector3{ 0.0f, 1.0f, 0.0f };
		camera.projection = CAMERA_PERSPECTIVE;

		ResetPlayerToSpawn();
	} break;

	case 2:
	{
		LoadingPulse("Loading base assets...", 0.20f, true);

		importAssets();

		LoadingPulse("Base assets loaded.", 0.35f, true);
	} break;
	case 3:
	{
		setLighting();
	} break;

	case 4:
	{
		LoadGachaMachineInstanceTest();
		LoadGachaBallInstanceTest();
	} break;

	case 5:
	{
		LoadBasketInstanceTest();
		LoadBasketCartridgeInstanceTest();
	} break;

	case 6:
	{
		CreateDefaultGachaInstanceProps();
		RebuildInstancedPropTransforms();
	} break;

	case 7:
	{
		createTestScene();
	} break;

	case 8:
	{
		BuildCustomerTypes();
		PreloadCustomerModelPool();
	} break;

	case 9:
	{
		BuildCustomers();
		BuildCustomerBodies();
	} break;

	case 10:
	{
		LoadingPulse("Building imported scene props...", 0.58f, true);

		BuildSceneProps();
		RefreshAllScenePropRenderBounds();

		LoadingPulse("Scene props ready.", 0.76f, true);
	} break;

	case 11:
	{
		BuildStoreBlockout();
		BuildItemPlacementSpots();
	} break;

	case 12:
	{
		BuildCustomerTradeItemCatalog();
		BuildCustomerDialogueTree();
		BuildDialogue();
		RefreshDialogueUILayout();
	} break;

	case 13:
	{
		LoadSceneState();
		RepairScenePropHierarchyLinks();
		PreparePickupScenePropsForPhysics();
		UpdateScenePropWorldTransforms();
		ApplyImportedEditorTransformsToRuntime();

		MarkShadowTextureBindingsDirty(false);

		InitializeRecordPlayerController();
		LoadRecordPlayerPlaylist();
	} break;

	case 14:
	{
		BuildStaticBodiesFromSceneProps();
		BuildStaticBodiesFromBlockout();

		customerNavGridDirty = true;
	} break;

	case 15:
	{
		pendingPostLoadPhysicsRestore = true;
		blockoutDirty = false;
		shadowMapsDirty = true;

		CreateVirtualPlayer();

		testDynamicBox = JPH::BodyID();
		hasTestDynamicBox = false;
	} break;

	case 16:
	{
		MarkShadowMapsDirty();

		// Final loading state.
		loadingStatus = "Ready.";
		loadingProgress = 1.0f;
	} break;
	}

	loadingStep++;
	loadingProgress = (float)loadingStep / (float)totalSteps;
	loadingStepPrimed = false;

	if (loadingStep >= totalSteps)
	{
		loadingStatus = "Ready.";
		loadingProgress = 1.0f;

		// Main path:
		FinishLoading();

		// Testing path, if needed:
		// SetScreen(GameScreen::Playing);
	}
}

void Game::DrawLoadingScreen()
{
	int screenW = GetScreenWidth();
	int screenH = GetScreenHeight();

	BeginDrawing();

	ClearBackground(Color{ 12, 14, 18, 255 });

	const char* title = "gemu shoppu";
	int titleSize = 56;
	int titleWidth = MeasureText(title, titleSize);

	DrawText(
		title,
		screenW / 2 - titleWidth / 2,
		screenH / 2 - 150,
		titleSize,
		WHITE
	);

	const char* loadingText = "Loading...";
	int loadingSize = 28;
	int loadingWidth = MeasureText(loadingText, loadingSize);

	DrawText(
		loadingText,
		screenW / 2 - loadingWidth / 2,
		screenH / 2 - 58,
		loadingSize,
		RAYWHITE
	);

	int statusSize = 20;
	int statusWidth = MeasureText(loadingStatus.c_str(), statusSize);

	DrawText(
		loadingStatus.c_str(),
		screenW / 2 - statusWidth / 2,
		screenH / 2 - 16,
		statusSize,
		Color{ 180, 190, 210, 255 }
	);

	float barW = 560.0f;
	float barH = 22.0f;
	float barX = screenW * 0.5f - barW * 0.5f;
	float barY = screenH * 0.5f + 44.0f;

	DrawRectangleRounded(
		{ barX, barY, barW, barH },
		0.35f,
		8,
		Color{ 35, 42, 55, 255 }
	);

	DrawRectangleRounded(
		{ barX, barY, barW * Clamp(loadingProgress, 0.0f, 1.0f), barH },
		0.35f,
		8,
		Color{ 80, 140, 220, 255 }
	);

	DrawRectangleRoundedLines(
		{ barX, barY, barW, barH },
		0.35f,
		8,
		Color{ 130, 160, 210, 255 }
	);

	const char* percentText = TextFormat("%i%%", (int)(Clamp(loadingProgress, 0.0f, 1.0f) * 100.0f));
	int percentWidth = MeasureText(percentText, 20);

	DrawText(
		percentText,
		screenW / 2 - percentWidth / 2,
		(int)(barY + 34.0f),
		20,
		RAYWHITE
	);

	float elapsed = (float)(GetTime() - loadingStartTime);
	const char* elapsedText = TextFormat("Elapsed: %.1fs", elapsed);
	int elapsedWidth = MeasureText(elapsedText, 18);

	DrawText(
		elapsedText,
		screenW / 2 - elapsedWidth / 2,
		(int)(barY + 64.0f),
		18,
		Color{ 160, 170, 190, 255 }
	);

	EndDrawing();
}

bool Game::IsMenuScreen(GameScreen screen) const
{
	return screen == GameScreen::MainMenu ||
		screen == GameScreen::Controls ||
		screen == GameScreen::AudioSettings ||
		screen == GameScreen::GraphicsSettings ||
		screen == GameScreen::Credits ||
		screen == GameScreen::Paused;
}

void Game::OpenGraphicsSettingsMenu(GameScreen returnScreen)
{
	menuReturnScreen = returnScreen;
	SetScreen(GameScreen::GraphicsSettings);
}

void Game::DrawGraphicsSettingsMenu()
{
	int screenW = GetScreenWidth();
	int screenH = GetScreenHeight();

	ClearBackground(Color{ 12, 14, 18, 255 });

	const char* title = "Graphics Settings";
	int titleSize = 48;
	int titleW = MeasureText(title, titleSize);

	DrawText(
		title,
		screenW / 2 - titleW / 2,
		90,
		titleSize,
		WHITE
	);

	DrawText(
		TextFormat("Screen: %i x %i", screenW, screenH),
		screenW / 2 - 160,
		165,
		22,
		Color{ 180, 190, 210, 255 }
	);

	DrawText(
		TextFormat("Current Render Target: %i x %i", renderTargetWidth, renderTargetHeight),
		screenW / 2 - 160,
		195,
		22,
		Color{ 180, 190, 210, 255 }
	);

	float buttonW = 420.0f;
	float buttonH = 48.0f;
	float x = screenW * 0.5f - buttonW * 0.5f;
	float y = 250.0f;
	float gap = 60.0f;

	auto drawModeButton = [&](RenderResolutionMode mode)
		{
			std::string label = "";

			if (renderResolutionMode == mode)
			{
				label += "> ";
			}

			label += GetRenderResolutionModeName(mode);

			if (DrawUIButton({ x, y, buttonW, buttonH }, label.c_str()))
			{
				SetRenderResolutionMode(mode);
			}

			y += gap;
		};

	drawModeButton(RenderResolutionMode::Native);
	drawModeButton(RenderResolutionMode::P1440);
	drawModeButton(RenderResolutionMode::P1080);
	drawModeButton(RenderResolutionMode::P720);
	drawModeButton(RenderResolutionMode::Scale75);
	drawModeButton(RenderResolutionMode::Scale50);

	y += 20.0f;

	if (DrawUIButton({ x, y, buttonW, buttonH }, "Back"))
	{
		SetScreen(menuReturnScreen);
	}

	DrawText(
		"Native gives best image quality. 1080p/720p improves laptop performance.",
		screenW / 2 - 360,
		screenH - 70,
		20,
		Color{ 160, 170, 190, 255 }
	);
}

void Game::OpenControlsMenu(GameScreen returnScreen)
{
	menuReturnScreen = returnScreen;
	SetScreen(GameScreen::Controls);
}

void Game::OpenAudioSettingsMenu(GameScreen returnScreen)
{
	menuReturnScreen = returnScreen;
	SetScreen(GameScreen::AudioSettings);
}

void Game::SetScreen(GameScreen screen)
{
	GameScreen previousScreen = currentScreen;

	bool wasMenu = IsMenuScreen(previousScreen);
	bool willBeMenu = IsMenuScreen(screen);

	bool wasMainMenuMusicScreen = IsMainMenuMusicScreen(previousScreen);
	bool willBeMainMenuMusicScreen = IsMainMenuMusicScreen(screen);

	// First time setup must always apply cursor state.
	bool firstSet = !hasInitializedScreen;
	hasInitializedScreen = true;

	currentScreen = screen;
	cursorUnlocked = false;

	if (!wasMainMenuMusicScreen && willBeMainMenuMusicScreen)
	{
		// Avoid record player music continuing underneath the main menu music.
		if (recordPlayerMusicLoaded && IsMusicStreamPlaying(recordPlayerMusic))
		{
			PauseMusicStream(recordPlayerMusic);
			recordPlayerPaused = true;
		}

		StartMainMenuMusic();
	}
	else if (wasMainMenuMusicScreen && !willBeMainMenuMusicScreen)
	{
		StopMainMenuMusic();
	}

	// Menu -> Menu
	// Example: Pause -> Controls, Controls -> Pause.
	// Do not call EnableCursor again, unless this is the first SetScreen call.
	if (!firstSet && wasMenu && willBeMenu)
	{
		return;
	}

	// Gameplay -> Menu, or first launch into menu
	if ((!wasMenu && willBeMenu) || (firstSet && willBeMenu))
	{
		if (cursorCapturedByGame)
		{
			EnableCursor();
			cursorCapturedByGame = false;
		}
		else if (IsCursorHidden())
		{
			EnableCursor();
		}

		return;
	}

	// Menu -> Gameplay
	if (wasMenu && !willBeMenu)
	{
		SetMousePosition(GetScreenWidth() / 2, GetScreenHeight() / 2);

		if (!cursorCapturedByGame)
		{
			DisableCursor();
			cursorCapturedByGame = true;
		}

		return;
	}
}

//UI
bool Game::DrawUIButton(Rectangle rect, const char* text)
{
	Vector2 mouse = GetMousePosition();
	bool hovered = CheckCollisionPointRec(mouse, rect);

	Color bg = hovered
		? Color{ 70, 105, 150, 255 }
	: Color{ 42, 58, 85, 255 };

	Color border = hovered
		? Color{ 180, 220, 255, 255 }
	: Color{ 100, 130, 170, 255 };

	DrawRectangleRounded(rect, 0.18f, 8, bg);
	DrawRectangleRoundedLines(rect, 0.18f, 8, border);

	int fontSize = 24;
	int textWidth = MeasureText(text, fontSize);

	DrawText(
		text,
		(int)(rect.x + rect.width * 0.5f - textWidth * 0.5f),
		(int)(rect.y + rect.height * 0.5f - fontSize * 0.5f),
		fontSize,
		WHITE
	);

	return hovered && IsMouseButtonReleased(MOUSE_LEFT_BUTTON);
}

void Game::PrepareMainMenuPBR(
	const Camera3D& menuCamera,
	int useAlbedo,
	int useNormal,
	int useMRA
)
{
	if (pbrShader.id == 0)
		return;

	// Reuse your inspect-style lighting because it is simple and camera-facing.
	UploadInspectionLightingToShader(menuCamera);

	float metallic = 0.0f;
	float roughness = 0.45f;
	float ao = 1.0f;
	float normalStrength = useNormal ? 1.0f : 0.0f;
	float emissiveIntensity = 0.0f;

	Vector4 albedoColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	Vector4 emissiveColor = { 0.0f, 0.0f, 0.0f, 1.0f };

	if (metallicValueLoc >= 0)
		SetShaderValue(pbrShader, metallicValueLoc, &metallic, SHADER_UNIFORM_FLOAT);

	if (roughnessValueLoc >= 0)
		SetShaderValue(pbrShader, roughnessValueLoc, &roughness, SHADER_UNIFORM_FLOAT);

	if (aoValueLoc >= 0)
		SetShaderValue(pbrShader, aoValueLoc, &ao, SHADER_UNIFORM_FLOAT);

	if (normalValueLoc >= 0)
		SetShaderValue(pbrShader, normalValueLoc, &normalStrength, SHADER_UNIFORM_FLOAT);

	if (emissiveIntensityLoc >= 0)
		SetShaderValue(pbrShader, emissiveIntensityLoc, &emissiveIntensity, SHADER_UNIFORM_FLOAT);

	if (albedoColorLoc >= 0)
		SetShaderValue(pbrShader, albedoColorLoc, &albedoColor, SHADER_UNIFORM_VEC4);

	if (emissiveColorLoc >= 0)
		SetShaderValue(pbrShader, emissiveColorLoc, &emissiveColor, SHADER_UNIFORM_VEC4);

	SetTextureUsage(
		pbrShader,
		useAlbedo,
		useNormal,
		useMRA,
		0
	);
}

void Game::UploadMainMenuLightingToShader(const Camera3D& menuCamera) const
{
	if (pbrShader.id == 0)
		return;

	Vector3 forward = NormalizeSafe(Vector3Subtract(menuCamera.target, menuCamera.position));
	Vector3 right = NormalizeSafe(Vector3CrossProduct(forward, { 0.0f, 1.0f, 0.0f }));

	if (Vector3Length(right) < 0.0001f)
	{
		right = { 1.0f, 0.0f, 0.0f };
	}

	Vector3 up = NormalizeSafe(Vector3CrossProduct(right, forward));

	// Key light: slightly above and to the camera-right.
	Vector3 keyLightPos = menuCamera.position;
	keyLightPos = Vector3Add(keyLightPos, Vector3Scale(forward, 1.2f));
	keyLightPos = Vector3Add(keyLightPos, Vector3Scale(right, 1.1f));
	keyLightPos = Vector3Add(keyLightPos, Vector3Scale(up, 1.2f));

	// Fill light: softer light from the opposite side.
	Vector3 fillLightPos = menuCamera.position;
	fillLightPos = Vector3Add(fillLightPos, Vector3Scale(forward, 2.0f));
	fillLightPos = Vector3Add(fillLightPos, Vector3Scale(right, -1.4f));
	fillLightPos = Vector3Add(fillLightPos, Vector3Scale(up, 0.45f));

	int menuLightCount = 2;
	SetShaderValue(pbrShader, lightCountLoc, &menuLightCount, SHADER_UNIFORM_INT);

	float cameraPos[3] = {
		menuCamera.position.x,
		menuCamera.position.y,
		menuCamera.position.z
	};

	SetShaderValue(
		pbrShader,
		pbrShader.locs[SHADER_LOC_VECTOR_VIEW],
		cameraPos,
		SHADER_UNIFORM_VEC3
	);

	Vector3 ambientColor = { 0.42f, 0.43f, 0.48f };
	float ambientIntensity = 0.42f;

	SetShaderValue(
		pbrShader,
		GetShaderLocation(pbrShader, "ambientColor"),
		&ambientColor,
		SHADER_UNIFORM_VEC3
	);

	SetShaderValue(
		pbrShader,
		GetShaderLocation(pbrShader, "ambient"),
		&ambientIntensity,
		SHADER_UNIFORM_FLOAT
	);

	int enabled = 1;
	int type = LIGHT_POINT;
	float target[3] = { 0.0f, 0.0f, 0.0f };

	float keyColor[4] = { 1.0f, 0.94f, 0.82f, 1.0f };
	float keyIntensity = 13.0f;
	float keyPos[3] = {
		keyLightPos.x,
		keyLightPos.y,
		keyLightPos.z
	};

	SetShaderValue(pbrShader, GetShaderLocation(pbrShader, "lights[0].enabled"), &enabled, SHADER_UNIFORM_INT);
	SetShaderValue(pbrShader, GetShaderLocation(pbrShader, "lights[0].type"), &type, SHADER_UNIFORM_INT);
	SetShaderValue(pbrShader, GetShaderLocation(pbrShader, "lights[0].position"), keyPos, SHADER_UNIFORM_VEC3);
	SetShaderValue(pbrShader, GetShaderLocation(pbrShader, "lights[0].target"), target, SHADER_UNIFORM_VEC3);
	SetShaderValue(pbrShader, GetShaderLocation(pbrShader, "lights[0].color"), keyColor, SHADER_UNIFORM_VEC4);
	SetShaderValue(pbrShader, GetShaderLocation(pbrShader, "lights[0].intensity"), &keyIntensity, SHADER_UNIFORM_FLOAT);

	float fillColor[4] = { 0.55f, 0.70f, 1.0f, 1.0f };
	float fillIntensity = 4.5f;
	float fillPos[3] = {
		fillLightPos.x,
		fillLightPos.y,
		fillLightPos.z
	};

	SetShaderValue(pbrShader, GetShaderLocation(pbrShader, "lights[1].enabled"), &enabled, SHADER_UNIFORM_INT);
	SetShaderValue(pbrShader, GetShaderLocation(pbrShader, "lights[1].type"), &type, SHADER_UNIFORM_INT);
	SetShaderValue(pbrShader, GetShaderLocation(pbrShader, "lights[1].position"), fillPos, SHADER_UNIFORM_VEC3);
	SetShaderValue(pbrShader, GetShaderLocation(pbrShader, "lights[1].target"), target, SHADER_UNIFORM_VEC3);
	SetShaderValue(pbrShader, GetShaderLocation(pbrShader, "lights[1].color"), fillColor, SHADER_UNIFORM_VEC4);
	SetShaderValue(pbrShader, GetShaderLocation(pbrShader, "lights[1].intensity"), &fillIntensity, SHADER_UNIFORM_FLOAT);

	int disabled = 0;

	for (int i = 2; i < MAX_LIGHTS; i++)
	{
		SetShaderValue(
			pbrShader,
			GetShaderLocation(pbrShader, TextFormat("lights[%i].enabled", i)),
			&disabled,
			SHADER_UNIFORM_INT
		);
	}
}

void Game::DrawMainMenu3DBackground()
{
	int screenW = GetScreenWidth();
	int screenH = GetScreenHeight();

	float t = (float)GetTime();

	DrawRectangleGradientV(
		0,
		0,
		screenW,
		screenH,
		Color{ 20, 24, 34, 255 },
		Color{ 5, 7, 12, 255 }
	);

	Camera3D menuCamera{};
	menuCamera.position = {
		3.4f + sinf(t * 0.18f) * 0.25f,
		1.7f + sinf(t * 0.35f) * 0.05f,
		4.8f
	};
	menuCamera.target = { 0.4f, 0.75f, 0.0f };
	menuCamera.up = { 0.0f, 1.0f, 0.0f };
	menuCamera.fovy = 42.0f;
	menuCamera.projection = CAMERA_PERSPECTIVE;

	BeginMode3D(menuCamera);

	switch (mainMenuSceneType)
	{
	case MainMenuSceneType::Showcase:
		DrawMainMenuShowcaseScene(menuCamera);
		break;

	case MainMenuSceneType::SavedSceneProps:
		DrawMainMenuSavedSceneProps(menuCamera);
		break;

	case MainMenuSceneType::GachaDisplay:
		DrawMainMenuGachaDisplayScene(menuCamera);
		break;
	}

	EndMode3D();

	// Left-side readability overlay.
	DrawRectangleGradientH(
		0,
		0,
		screenW,
		screenH,
		Color{ 4, 6, 10, 220 },
		Color{ 4, 6, 10, 20 }
	);

	// Reduce this if the scene is still too dark.
	DrawRectangle(
		0,
		0,
		screenW,
		screenH,
		Color{ 0, 0, 0, 12 }
	);

	// Restore normal gameplay lighting after menu rendering.
	if (pbrShader.id != 0)
	{
		UploadWorldLightingToShader();
	}
}
void Game::DrawMainMenuShowcaseScene(const Camera3D& menuCamera)
{
	float t = (float)GetTime();

	DrawPlane(
		{ 0.8f, -0.03f, 0.0f },
		{ 8.0f, 8.0f },
		Color{ 35, 39, 50, 255 }
	);

	//DrawGrid(10, 1.0f);

	if (Shelf.meshCount > 0)
	{
		PrepareMainMenuPBR(menuCamera, 1, 1, 1);

		DrawModelEuler(
			Shelf,
			{ 1.35f, 0.0f, -0.55f },
			{ 0.0f, -28.0f, 0.0f },
			{ 0.65f, 0.65f, 0.65f },
			WHITE
		);
	}

	if (gBoy.meshCount > 0)
	{
		PrepareMainMenuPBR(menuCamera, 1, 1, 1);

		DrawModelEuler(
			gBoy,
			{
				-0.25f,
				0.70f + sinf(t * 1.2f) * 0.06f,
				0.15f
			},
			{
				0.0f,
				25.0f + t * 18.0f,
				0.0f
			},
			{ 1.0f, 1.0f, 1.0f },
			WHITE
		);
	}

	if (Gacha.meshCount > 0)
	{
		PrepareMainMenuPBR(menuCamera, 1, 0, 1);

		DrawModelEuler(
			Gacha,
			{ 2.05f, 0.0f, 0.35f },
			{ 0.0f, -20.0f, 0.0f },
			{ 0.65f, 0.65f, 0.65f },
			WHITE
		);
	}
}

void Game::DrawMainMenuSavedSceneProps(const Camera3D& menuCamera)
{
	DrawPlane(
		{ 0.8f, -0.03f, 0.0f },
		{ 10.0f, 10.0f },
		Color{ 35, 39, 50, 255 }
	);

	//DrawGrid(12, 1.0f);

	// Make sure instance transforms are current.
	if (instancedPropsDirty)
	{
		RebuildInstancedPropTransforms();
	}

	// -----------------------------
	// Instanced props
	// -----------------------------
	// Draw balls/cartridges before machines/baskets so the transparent shell
	// can still show the contents correctly.
	DrawGachaBallInstances(menuCamera);
	DrawGachaMachineInstanceTest(menuCamera);

	DrawBasketCartridgeInstances(menuCamera);
	DrawBasketInstances(menuCamera);

	// -----------------------------
	// Normal scene props
	// -----------------------------
	PrepareMainMenuPBR(menuCamera, 1, 1, 1);

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		if (!sceneProps[i].visible)
			continue;

		if (sceneProps[i].model == nullptr)
			continue;

		DrawScenePropByIndexPass(i, false);
	}

	BeginBlendMode(BLEND_ALPHA);
	rlDisableDepthMask();
	rlDisableBackfaceCulling();

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		if (!sceneProps[i].visible)
			continue;

		if (sceneProps[i].model == nullptr)
			continue;

		DrawScenePropByIndexPass(i, true);
	}

	rlEnableBackfaceCulling();
	rlEnableDepthMask();
	EndBlendMode();
}

void Game::DrawMainMenuGachaDisplayScene(const Camera3D& menuCamera)
{
	float t = (float)GetTime();

	DrawPlane(
		{ 0.8f, -0.03f, 0.0f },
		{ 8.0f, 8.0f },
		Color{ 32, 36, 46, 255 }
	);

	DrawGrid(10, 1.0f);

	if (Gacha.meshCount > 0)
	{
		PrepareMainMenuPBR(menuCamera, 1, 0, 1);

		DrawModelEuler(
			Gacha,
			{ 0.85f, 0.0f, 0.15f },
			{ 0.0f, 180.0f + sinf(t * 0.8f) * 8.0f, 0.0f },
			{ 0.95f, 0.95f, 0.95f },
			WHITE
		);
	}

	if (gBoy.meshCount > 0)
	{
		PrepareMainMenuPBR(menuCamera, 1, 1, 1);

		DrawModelEuler(
			gBoy,
			{ -0.75f, 0.65f, 0.25f },
			{ 0.0f, t * 22.0f, 0.0f },
			{ 0.85f, 0.85f, 0.85f },
			WHITE
		);
	}
}


void Game::DrawMainMenu()
{
	int screenW = GetScreenWidth();
	int screenH = GetScreenHeight();

	DrawMainMenu3DBackground();

	float menuX = fmaxf(70.0f, screenW * 0.10f);

	const char* title = "gemu shoppu";
	int titleSize = 64;

	DrawText(
		title,
		(int)menuX,
		115,
		titleSize,
		WHITE
	);

	const char* subtitle = "Game Jam edition";
	int subtitleSize = 24;

	DrawText(
		subtitle,
		(int)menuX + 4,
		195,
		subtitleSize,
		Color{ 190, 200, 220, 255 }
	);

	float buttonW = 340.0f;
	float buttonH = 58.0f;
	float buttonX = menuX;
	float buttonY = 285.0f;
	float gap = 66.0f;

	if (DrawUIButton({ buttonX, buttonY, buttonW, buttonH }, "Play"))
	{
		startNewGameRequested = true;
	}

	if (DrawUIButton({ buttonX, buttonY + gap, buttonW, buttonH }, "Controls"))
	{
		OpenControlsMenu(GameScreen::MainMenu);
	}

	if (DrawUIButton({ buttonX, buttonY + gap * 2.0f, buttonW, buttonH }, "Audio Settings"))
	{
		OpenAudioSettingsMenu(GameScreen::MainMenu);
	}

	if (DrawUIButton({ buttonX, buttonY + gap * 3.0f, buttonW, buttonH }, "Credits"))
	{
		menuReturnScreen = GameScreen::MainMenu;
		SetScreen(GameScreen::Credits);
	}


	if (DrawUIButton({ buttonX, buttonY + gap * 4.0f, buttonW, buttonH }, "Graphics"))
	{
		OpenGraphicsSettingsMenu(GameScreen::MainMenu);
	}

	if (DrawUIButton({ buttonX, buttonY + gap * 5.0f, buttonW, buttonH }, "Quit"))
	{
		requestQuit = true;
	}

	// Short visible credits on main menu.
	int creditX = (int)menuX + 4;
	int creditY = screenH - 120;
	int creditLine = 23;



	DrawText(
		"Made by Sean (Segton)",
		creditX,
		creditY,
		18,
		Color{ 215, 225, 240, 255 }
	);

	DrawText(
		"3D Modelling: Reshika",
		creditX,
		creditY + creditLine,
		18,
		Color{ 180, 190, 210, 255 }
	);

	DrawText(
		"Concept Art: Shu Zanne",
		creditX,
		creditY + creditLine * 2,
		18,
		Color{ 180, 190, 210, 255 }
	);
	DrawText(
		"Menu Music: toby fox - UNDERTALE Soundtrack - 23 Shop",
		creditX,
		creditY + creditLine * 3,
		16,
		Color{ 150, 165, 190, 255 }
	);
}

void Game::DrawControlsMenu()
{
	int screenW = GetScreenWidth();

	DrawText(
		"Controls",
		screenW / 2 - MeasureText("Controls", 48) / 2,
		100,
		48,
		WHITE
	);

	int x = screenW / 2 - 280;
	int y = 200;
	int line = 38;

	DrawText("WASD                 Move", x, y, 22, RAYWHITE);
	DrawText("Mouse                Look Around", x, y + line, 22, RAYWHITE);
	DrawText("E                    Pick Up / Interact", x, y + line * 2, 22, RAYWHITE);
	DrawText("F                    Inspect Held Object", x, y + line * 3, 22, RAYWHITE);
	DrawText("Left Mouse Drag      Rotate Inspected Object", x, y + line * 4, 22, RAYWHITE);
	DrawText("Mouse Wheel          Zoom During Inspect", x, y + line * 5, 22, RAYWHITE);
	DrawText("P                    Pause", x, y + line * 6, 22, RAYWHITE);
	DrawText("L Alt                  Back / Resume", x, y + line * 7, 22, RAYWHITE);

	if (DrawUIButton({ screenW / 2 - 160.0f, 580.0f, 320.0f, 56.0f }, "Back"))
	{
		SetScreen(menuReturnScreen);
	}
}


void Game::DrawAudioSettingsMenu()
{
	int screenW = GetScreenWidth();

	const char* title = "Audio Settings";
	int titleSize = 44;
	int titleW = MeasureText(title, titleSize);

	DrawText(title, screenW / 2 - titleW / 2, 90, titleSize, WHITE);

	float x = screenW / 2.0f - 220.0f;
	float y = 220.0f;

	if (DrawUISlider({ x, y, 440.0f, 18.0f }, "Master Volume", masterVolume))
	{
		SetMasterVolume(masterVolume);
		UpdateRecordPlayerMusicVolume();
	}

	if (DrawUISlider({ x, y + 90.0f, 440.0f, 18.0f }, "Music Volume", musicVolume))
	{
		UpdateRecordPlayerMusicVolume();
	}

	if (DrawUISlider({ x, y + 180.0f, 440.0f, 18.0f }, "SFX Volume", soundFxVolume))
	{
		// Later: SetSoundVolume(yourSound, sfxVolume * masterVolume);
	}

	if (DrawUIButton({ screenW / 2 - 160.0f, 580.0f, 320.0f, 56.0f }, "Back"))
	{
		SetScreen(menuReturnScreen);
	}
}

void Game::DrawPauseMenu()
{
	int screenW = GetScreenWidth();
	int screenH = GetScreenHeight();

	DrawRectangle(0, 0, screenW, screenH, Color{ 0, 0, 0, 150 });

	const char* title = "Paused";
	int titleSize = 52;
	int titleWidth = MeasureText(title, titleSize);

	DrawText(
		title,
		screenW / 2 - titleWidth / 2,
		120,
		titleSize,
		WHITE
	);

	float buttonW = 340.0f;
	float buttonH = 58.0f;
	float buttonX = screenW * 0.5f - buttonW * 0.5f;
	float buttonY = 240.0f;
	float gap = 74.0f;

	if (DrawUIButton({ buttonX, buttonY, buttonW, buttonH }, "Resume"))
	{
		ResumeGame();
	}

	if (DrawUIButton({ buttonX, buttonY + gap, buttonW, buttonH }, "Controls"))
	{
		OpenControlsMenu(GameScreen::Paused);
	}

	if (DrawUIButton({ buttonX, buttonY + gap * 2.0f, buttonW, buttonH }, "Audio Settings"))
	{
		OpenAudioSettingsMenu(GameScreen::Paused);
	}
	if (DrawUIButton({ buttonX, buttonY + gap * 3.f, buttonW, buttonH }, "Graphics"))
	{
		OpenGraphicsSettingsMenu(GameScreen::Paused);
	}

	if (DrawUIButton({ buttonX, buttonY + gap * 4.0f, buttonW, buttonH }, "Return to Main Menu"))
	{
		ReturnToMainMenu();
	}

	if (DrawUIButton({ buttonX, buttonY + gap * 5.0f, buttonW, buttonH }, "Quit"))
	{
		requestQuit = true;
	}
}

void Game::DrawCreditsMenu()
{
	int screenW = GetScreenWidth();
	int screenH = GetScreenHeight();

	DrawText(
		"Credits",
		screenW / 2 - MeasureText("Credits", 52) / 2,
		80,
		52,
		WHITE
	);

	int x = screenW / 2 - 360;
	int y = 170;
	int line = 34;

	auto DrawCreditRow = [&](const char* role, const char* name, int rowY)
		{
			int roleSize = 21;
			int nameSize = 22;

			int roleX = x;
			int nameX = x + 560;

			DrawText(
				role,
				roleX,
				rowY,
				roleSize,
				Color{ 180, 190, 210, 255 }
			);

			DrawText(
				name,
				nameX,
				rowY,
				nameSize,
				WHITE
			);
		};

	DrawText("Project Credits", x, y, 28, RAYWHITE);

	y += 55;

	DrawCreditRow(
		"Development / Programming / Game Design",
		"Sean (Segton)",
		y
	);

	y += line;

	DrawCreditRow(
		"3D Modelling",
		"Reshika",
		y
	);

	y += line;

	DrawCreditRow(
		"Concept Art",
		"Lim Shu Zanne",
		y
	);

	y += line * 2;

	DrawText("Asset Credits", x, y, 28, RAYWHITE);

	y += 50;

	DrawCreditRow(
		"Art Assets",
		"To be listed",
		y
	);

	y += line;

	DrawCreditRow(
		"Sound Assets",
		"To be listed",
		y
	);

	y += line;

	DrawCreditRow(
		"Music",
		"To be listed",
		y
	);

	y += line * 2;

	DrawText(
		"More detailed credits and asset licences will be added later.",
		x,
		y,
		20,
		Color{ 160, 170, 190, 255 }
	);

	if (DrawUIButton({ screenW / 2 - 160.0f, (float)screenH - 100.0f, 320.0f, 56.0f }, "Back"))
	{
		SetScreen(menuReturnScreen);
	}
}

//end UI
void Game::UploadLightsToShader(Shader shader) const
{
	const LightUniformLocations* locs = nullptr;

	if (shader.id == pbrShader.id)
	{
		locs = &pbrLightLocs;
	}
	else if (shader.id == animatedPbrShader.id)
	{
		locs = &animatedLightLocs;
	}
	else if (shader.id == instancedPbrShader.id)
	{
		locs = &instancedLightLocs;
	}

	if (locs == nullptr)
		return;

	SetShaderValueIfValid(
		shader,
		locs->numOfLights,
		&lightCount,
		SHADER_UNIFORM_INT
	);

	float cameraPos[3] = {
		camera.position.x,
		camera.position.y,
		camera.position.z
	};

	SetShaderValueIfValid(
		shader,
		locs->viewPos,
		cameraPos,
		SHADER_UNIFORM_VEC3
	);

	float ambientColor[3] = {
		worldAmbientColorCached.x,
		worldAmbientColorCached.y,
		worldAmbientColorCached.z
	};

	SetShaderValueIfValid(
		shader,
		locs->ambientColor,
		ambientColor,
		SHADER_UNIFORM_VEC3
	);

	SetShaderValueIfValid(
		shader,
		locs->ambient,
		&worldAmbientIntensityCached,
		SHADER_UNIFORM_FLOAT
	);

	for (int i = 0; i < lightCount && i < MAX_LIGHTS; i++)
	{
		SetShaderValueIfValid(
			shader,
			locs->enabled[i],
			&lights[i].enabled,
			SHADER_UNIFORM_INT
		);

		SetShaderValueIfValid(
			shader,
			locs->type[i],
			&lights[i].type,
			SHADER_UNIFORM_INT
		);

		float pos[3] = {
			lights[i].position.x,
			lights[i].position.y,
			lights[i].position.z
		};

		float target[3] = {
			lights[i].target.x,
			lights[i].target.y,
			lights[i].target.z
		};

		SetShaderValueIfValid(
			shader,
			locs->position[i],
			pos,
			SHADER_UNIFORM_VEC3
		);

		SetShaderValueIfValid(
			shader,
			locs->target[i],
			target,
			SHADER_UNIFORM_VEC3
		);

		SetShaderValueIfValid(
			shader,
			locs->color[i],
			lights[i].color,
			SHADER_UNIFORM_VEC4
		);

		SetShaderValueIfValid(
			shader,
			locs->intensity[i],
			&lights[i].intensity,
			SHADER_UNIFORM_FLOAT
		);

		SetShaderValueIfValid(
			shader,
			locs->range[i],
			&lights[i].range,
			SHADER_UNIFORM_FLOAT
		);
	}
}
float Game::GetPickupOutlinePulse01() const
{
	if (!pickupOutlinePulseEnabled) return 1.0f;

	float t = (float)GetTime() * pickupOutlinePulseSpeed;
	return (sinf(t) + 1.0f) * 0.5f;
}

float Game::GetCurrentVisibleOutlineWidth() const
{
	if (!pickupOutlinePulseEnabled)
		return pickupOutlineWidth;

	float t = GetPickupOutlinePulse01();
	return Lerp(pickupOutlinePulseMinWidth, pickupOutlinePulseMaxWidth, t);
}

Color Game::GetCurrentVisibleOutlineColor() const
{
	Color c = pickupOutlineColor;

	if (!pickupOutlinePulseEnabled)
		return c;

	float t = GetPickupOutlinePulse01();
	float alpha = Lerp(
		(float)pickupOutlinePulseMinAlpha,
		(float)pickupOutlinePulseMaxAlpha,
		t
	);

	c.a = (unsigned char)Clamp(alpha, 0.0f, 255.0f);
	return c;
}
void Game::MarkShadowTextureBindingsDirty(bool shadowTextureRecreated)
{
	shadowTextureBindingsDirty = true;

	if (shadowTextureRecreated)
	{
		lastAttachedShadowTextureId = 0;
	}
}
void Game::AttachShadowTextureToSceneMaterials()
{
	Texture2D shadowTexture = shadowCasters[0].shadowMap.texture;

	if (shadowTexture.id == 0)
		return;

	if (!shadowTextureBindingsDirty &&
		lastAttachedShadowTextureId == shadowTexture.id)
	{
		return;
	}

	AttachShadowTextureToModel(pbrFloor, shadowTexture);
	AttachShadowTextureToModel(testProp, shadowTexture);
	AttachShadowTextureToModel(blockoutCubeModel, shadowTexture);
	AttachShadowTextureToModel(Shelf, shadowTexture);
	AttachShadowTextureToModel(gBoy, shadowTexture);
	AttachShadowTextureToModel(Gacha, shadowTexture);
	AttachShadowTextureToModel(customerModel, shadowTexture);

	for (auto& modelPtr : customerModels)
	{
		if (modelPtr != nullptr)
		{
			AttachShadowTextureToModel(*modelPtr, shadowTexture);
		}
	}

	for (SceneProp& prop : sceneProps)
	{
		if (prop.model != nullptr)
		{
			AttachShadowTextureToModel(*prop.model, shadowTexture);
		}
	}

	if (gachaInstanceBatch.loaded)
	{
		AttachShadowTextureToModel(gachaInstanceBatch.model, shadowTexture);
	}

	if (gachaBallInstanceBatch.loaded)
	{
		AttachShadowTextureToModel(gachaBallInstanceBatch.model, shadowTexture);
	}

	if (basketInstanceBatch.loaded)
	{
		AttachShadowTextureToModel(basketInstanceBatch.model, shadowTexture);
	}

	if (basketCartridgeInstanceBatch.loaded)
	{
		AttachShadowTextureToModel(basketCartridgeInstanceBatch.model, shadowTexture);
	}

	lastAttachedShadowTextureId = shadowTexture.id;
	shadowTextureBindingsDirty = false;
}

void Game::MarkShadowMapsDirty()
{
	shadowMapsDirty = true;
}

bool Game::HasDynamicShadowCasters() const
{
	if (hasHeldBody)
		return true;

	if (!customers.empty() && shadowMapsUpdateForCustomers)
		return true;

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		const SceneProp& prop = sceneProps[i];

		if (ScenePropUsesDynamicShadowPass(i, prop))
			return true;
	}

	return false;
}
bool Game::ScenePropUsesDynamicShadowPass(int propIndex, const SceneProp& prop) const
{
	if (!prop.visible)
		return false;

	if (prop.model == nullptr)
		return false;

	if (!prop.castsShadow)
		return false;

	// Held object must be dynamic, otherwise the static cache keeps
	// its old shadow on the floor.
	if (hasHeldBody && propIndex == heldScenePropIndex)
		return true;

	// Physics-driven props should be dynamic.
	if (prop.simulatePhysics && !prop.editLockPhysics && !prop.bodyId.IsInvalid())
		return true;

	return false;
}

void Game::RenderShadowMapsIfNeeded()
{
	bool staticNeedsUpdate =
		shadowMapsDirty ||
		shadowMapsUpdateEveryFrame;

	bool dynamicNeedsUpdate =
		shadowMapsUpdateEveryFrame ||
		(shadowMapsUpdateForCustomers && !customers.empty()) ||
		(shadowMapsUpdateWhenDynamicPropsExist && HasDynamicShadowCasters());

	if (!staticNeedsUpdate && !dynamicNeedsUpdate)
		return;

	UpdateShadowCasters();

	if (staticNeedsUpdate)
	{
		RenderStaticShadowCache();
		shadowMapsDirty = false;
	}

	// The final map is rebuilt every dynamic frame,
	// but static scene props are NOT redrawn.
	CopyStaticShadowCacheToFinalShadowMap();

	if (dynamicNeedsUpdate)
	{
		RenderDynamicShadowsOntoFinalShadowMap();
	}
}

void Game::CacheShadowUniformLocations(
	Shader shader,
	ShadowUniformLocations& locs
)
{
	locs.shadowCasterCount = GetShaderLocation(shader, "shadowCasterCount");
	locs.shadowLightVP0 = GetShaderLocation(shader, "shadowLightVP[0]");
	locs.shadowLightIndex0 = GetShaderLocation(shader, "shadowLightIndex[0]");
	locs.shadowBias0 = GetShaderLocation(shader, "shadowBias[0]");
	locs.shadowStrength0 = GetShaderLocation(shader, "shadowStrength[0]");
	locs.shadowMap0 = GetShaderLocation(shader, "shadowMap0");
}

void Game::UpdateShadowCasters()
{
	for (int i = 0; i < MAX_SHADOW_CASTERS; i++)
	{
		shadowCasters[i].active = false;
		shadowCasters[i].lightIndex = -1;
	}

	if (lightCount <= 0) return;
	if (!lights[0].enabled) return;
	if (!lights[0].castsShadow) return;

	ShadowCaster& caster = shadowCasters[0];

	caster.active = true;
	caster.lightIndex = 0;
	caster.bias = lights[0].shadowBias;
	caster.strength = lights[0].shadowStrength;

	Vector3 lightDir = NormalizeSafe(
		Vector3Subtract(lights[0].target, lights[0].position)
	);

	Vector3 shadowCenter = lights[0].target;

	// Put the shadow camera halfway inside the shadow depth range.
	// This avoids wasting almost all of shadowFar before reaching the scene.
	float cameraDistance = lights[0].shadowFar * 0.5f;

	caster.camera.position = Vector3Subtract(
		shadowCenter,
		Vector3Scale(lightDir, cameraDistance)
	);

	caster.camera.target = shadowCenter;
	caster.camera.up = { 0.0f, 1.0f, 0.0f };
	caster.camera.projection = CAMERA_ORTHOGRAPHIC;
	caster.camera.fovy = lights[0].shadowOrthoSize;
}

void Game::RenderShadowMaps()
{
	for (int i = 0; i < MAX_SHADOW_CASTERS; i++)
	{
		ShadowCaster& caster = shadowCasters[i];

		if (!caster.active) continue;

		int lightIndex = caster.lightIndex;

		float orthoSize = lights[lightIndex].shadowOrthoSize;
		float half = orthoSize * 0.5f;

		Matrix lightProjection = MatrixOrtho(
			-half,
			half,
			-half,
			half,
			lights[lightIndex].shadowNear,
			lights[lightIndex].shadowFar
		);

		BeginTextureMode(caster.shadowMap);

		rlEnableDepthTest();
		rlEnableDepthMask();

		// White means "empty / far / no caster"
		rlClearColor(255, 255, 255, 255);
		rlClearScreenBuffers();

		BeginMode3D(caster.camera);

		rlSetMatrixProjection(lightProjection);

		caster.lightVP = MatrixMultiply(
			rlGetMatrixModelview(),
			rlGetMatrixProjection()
		);

		DrawShadowCasters();

		EndMode3D();

		EndTextureMode();
	}
}
void Game::DrawStaticShadowCasters()
{
	shadowStaticPropsDrawn = 0;

	rlEnableBackfaceCulling();
	rlSetCullFace(RL_CULL_FACE_FRONT);

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		SceneProp& prop = sceneProps[i];

		if (!prop.visible) continue;
		if (prop.model == nullptr) continue;

		// Important: do not bake movable props into the static cache.
		if (ScenePropUsesDynamicShadowPass(i, prop))
			continue;

		if (!ShouldScenePropCastShadow(prop, lights[0]))
			continue;

		shadowStaticPropsDrawn++;
		DrawScenePropShadow(prop);
	}

	// Static instanced objects go into the static shadow cache.
	DrawInstancedBatchShadow(gachaInstanceBatch);
	DrawInstancedBatchShadow(gachaBallInstanceBatch);

	rlSetCullFace(RL_CULL_FACE_BACK);
	rlDisableBackfaceCulling();
}

void Game::RenderDynamicShadowsOntoFinalShadowMap()
{
	ShadowCaster& caster = shadowCasters[0];

	if (!caster.active)
		return;

	int lightIndex = caster.lightIndex;

	float orthoSize = lights[lightIndex].shadowOrthoSize;
	float half = orthoSize * 0.5f;

	Matrix lightProjection = MatrixOrtho(
		-half,
		half,
		-half,
		half,
		lights[lightIndex].shadowNear,
		lights[lightIndex].shadowFar
	);

	BeginTextureMode(caster.shadowMap);

	BeginMode3D(caster.camera);

	rlSetMatrixProjection(lightProjection);

	// Do not use depth test here.
	// MIN blending makes the final texture keep the nearest/lowest depth value.
	rlDisableDepthTest();
	rlDisableDepthMask();

	BeginBlendMode(BLEND_CUSTOM);
	rlSetBlendFactors(RL_ONE, RL_ONE, RL_MIN);

	DrawDynamicShadowCasters();

	EndBlendMode();

	rlEnableDepthMask();
	rlEnableDepthTest();

	EndMode3D();

	EndTextureMode();
}

void Game::RenderStaticShadowCache()
{
	UpdateShadowCasters();

	ShadowCaster& caster = shadowCasters[0];

	if (!caster.active)
		return;

	int lightIndex = caster.lightIndex;

	float orthoSize = lights[lightIndex].shadowOrthoSize;
	float half = orthoSize * 0.5f;

	Matrix lightProjection = MatrixOrtho(
		-half,
		half,
		-half,
		half,
		lights[lightIndex].shadowNear,
		lights[lightIndex].shadowFar
	);

	// Render static scene props into shadowCasters[1].
	BeginTextureMode(shadowCasters[1].shadowMap);

	rlEnableDepthTest();
	rlEnableDepthMask();

	rlClearColor(255, 255, 255, 255);
	rlClearScreenBuffers();

	BeginMode3D(caster.camera);

	rlSetMatrixProjection(lightProjection);

	// This is still the matrix used by the final receiver shader.
	caster.lightVP = MatrixMultiply(
		rlGetMatrixModelview(),
		rlGetMatrixProjection()
	);

	DrawStaticShadowCasters();

	EndMode3D();

	EndTextureMode();
}
void Game::CopyStaticShadowCacheToFinalShadowMap()
{
	RenderTexture2D& finalMap = shadowCasters[0].shadowMap;
	RenderTexture2D& staticCache = shadowCasters[1].shadowMap;

	if (finalMap.texture.id == 0 || staticCache.texture.id == 0)
		return;

	BeginTextureMode(finalMap);

	ClearBackground(WHITE);

	Rectangle src = {
		0.0f,
		0.0f,
		(float)staticCache.texture.width,
		-(float)staticCache.texture.height
	};

	Rectangle dst = {
		0.0f,
		0.0f,
		(float)finalMap.texture.width,
		(float)finalMap.texture.height
	};

	rlDisableDepthTest();
	rlDisableDepthMask();

	DrawTexturePro(
		staticCache.texture,
		src,
		dst,
		{ 0.0f, 0.0f },
		0.0f,
		WHITE
	);

	rlEnableDepthMask();
	rlEnableDepthTest();

	EndTextureMode();
}

void Game::DrawInstancedBatchShadow(InstancedModelBatch& batch)
{
	if (!batch.loaded)
		return;

	if (batch.model.meshCount <= 0)
		return;

	if (batch.transforms.empty())
		return;

	Shader oldShaders[64];

	int savedCount = batch.model.materialCount;
	if (savedCount > 64)
		savedCount = 64;

	for (int i = 0; i < savedCount; i++)
	{
		oldShaders[i] = batch.model.materials[i].shader;
		batch.model.materials[i].shader = shadowDepthShader;
	}

	for (const Matrix& transform : batch.transforms)
	{
		for (int meshIndex = 0; meshIndex < batch.model.meshCount; meshIndex++)
		{
			int matIndex = 0;

			if (batch.model.meshMaterial != nullptr)
				matIndex = batch.model.meshMaterial[meshIndex];

			if (matIndex < 0 || matIndex >= batch.model.materialCount)
				matIndex = 0;

			DrawMesh(
				batch.model.meshes[meshIndex],
				batch.model.materials[matIndex],
				transform
			);
		}
	}

	for (int i = 0; i < savedCount; i++)
	{
		batch.model.materials[i].shader = oldShaders[i];
	}
}

void Game::DrawDynamicShadowCasters()
{
	shadowDynamicCustomersDrawn = 0;
	shadowDynamicPropsDrawn = 0;

	for (Customer& customer : customers)
	{
		if (!customer.model) continue;

		shadowDynamicCustomersDrawn++;
		customer.DrawWithShader(shadowDepthAnimatedShader);
	}

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		SceneProp& prop = sceneProps[i];

		if (!ScenePropUsesDynamicShadowPass(i, prop))
			continue;

		if (!ShouldScenePropCastShadow(prop, lights[0]))
			continue;

		shadowDynamicPropsDrawn++;
		DrawScenePropShadow(prop);
	}
}
void Game::DrawShadowCasters()
{
	// Customers: draw normally into shadow map.
	// They do not receive shadows in pbr_animated.fs, so self-shadow is already avoided.
	rlEnableBackfaceCulling();
	rlSetCullFace(RL_CULL_FACE_FRONT);

	for (SceneProp& prop : sceneProps)
	{
		if (!prop.visible) continue;
		if (prop.model == nullptr) continue;
		if (!ShouldScenePropCastShadow(prop, lights[0]))
			continue;

		DrawScenePropShadow(prop);
	}

	rlSetCullFace(RL_CULL_FACE_BACK);
	rlDisableBackfaceCulling();

	for (Customer& customer : customers)
	{
		if (!customer.model) continue;
		customer.DrawWithShader(shadowDepthAnimatedShader);
	}

	// Static props: use front-face culling to reduce self-shadow acne.

}

void Game::DrawCustomers() const
{
	UploadLightsToShader(animatedPbrShader);

	for (const Customer& customer : customers)
	{
		PrepareAnimatedCustomerPBRMaterial();

		if (customer.model != nullptr)
		{
			int useTexNormal =
				ModelHasRealTexture(*customer.model, MATERIAL_MAP_NORMAL) ? 1 : 0;

			int hasMetallic =
				ModelHasRealTexture(*customer.model, MATERIAL_MAP_METALNESS) ? 1 : 0;

			int hasRoughness =
				ModelHasRealTexture(*customer.model, MATERIAL_MAP_ROUGHNESS) ? 1 : 0;

			int hasAO =
				ModelHasRealTexture(*customer.model, MATERIAL_MAP_OCCLUSION) ? 1 : 0;

			int hasEmissive =
				ModelHasRealTexture(*customer.model, MATERIAL_MAP_EMISSION) ? 1 : 0;

			int useTexMRA = 0;
			int useTexMetallic = hasMetallic;
			int useTexRoughness = hasRoughness;
			int useTexAO = hasAO;
			int useTexEmissive = hasEmissive;

			SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexNormal"), &useTexNormal, SHADER_UNIFORM_INT);
			SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexMRA"), &useTexMRA, SHADER_UNIFORM_INT);
			SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexMetallic"), &useTexMetallic, SHADER_UNIFORM_INT);
			SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexRoughness"), &useTexRoughness, SHADER_UNIFORM_INT);
			SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexAO"), &useTexAO, SHADER_UNIFORM_INT);
			SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexEmissive"), &useTexEmissive, SHADER_UNIFORM_INT);
			for (int i = 0; i < customer.model->materialCount; i++)
			{
				customer.model->materials[i].shader = animatedPbrShader;
			}
		}

		float metallicValue = customer.pbrMetallicValue;
		float roughnessValue = customer.pbrRoughnessValue;
		float roughnessScale = customer.pbrRoughnessScale;
		float reflectionStrength = customer.pbrReflectionStrength;
		float emissivePower = customer.pbrEmissivePower;

		SetShaderValue(
			animatedPbrShader,
			GetShaderLocation(animatedPbrShader, "metallicValue"),
			&metallicValue,
			SHADER_UNIFORM_FLOAT
		);

		SetShaderValue(
			animatedPbrShader,
			GetShaderLocation(animatedPbrShader, "roughnessValue"),
			&roughnessValue,
			SHADER_UNIFORM_FLOAT
		);

		SetShaderValue(
			animatedPbrShader,
			GetShaderLocation(animatedPbrShader, "roughnessScale"),
			&roughnessScale,
			SHADER_UNIFORM_FLOAT
		);

		SetShaderValue(
			animatedPbrShader,
			GetShaderLocation(animatedPbrShader, "reflectionStrength"),
			&reflectionStrength,
			SHADER_UNIFORM_FLOAT
		);

		SetShaderValue(
			animatedPbrShader,
			GetShaderLocation(animatedPbrShader, "emissivePower"),
			&emissivePower,
			SHADER_UNIFORM_FLOAT
		);

		const_cast<Customer&>(customer).Draw();

		if (editMode && drawCustomerInteractBounds)
		{
			DrawBoundingBox(
				customer.GetTalkBounds(),
				customer.CanTalk() ? GREEN : RED
			);
		}
	}
}


void Game::DrawScenePropOutlineByIndex(int propIndex, const Camera3D& cam) const
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size()) return;

	const SceneProp& prop = sceneProps[propIndex];
	if (!prop.visible || prop.model == nullptr) return;

	Matrix transform = GetScenePropDrawMatrix(prop);

	DrawScenePropOutlineWithMatrix(
		propIndex,
		transform,
		cam,
		true
	);
}

bool Game::GetScenePropScreenRect(
	int propIndex,
	const Camera3D& cam,
	Rectangle& outRect,
	float& outDepth
) const
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return false;

	const SceneProp& prop = sceneProps[propIndex];

	if (!prop.visible || prop.model == nullptr)
		return false;

	BoundingBox bounds{};

	if (!GetScenePropRenderBoundsWorld(prop, bounds))
		return false;

	Vector3 corners[8] =
	{
		{ bounds.min.x, bounds.min.y, bounds.min.z },
		{ bounds.max.x, bounds.min.y, bounds.min.z },
		{ bounds.min.x, bounds.max.y, bounds.min.z },
		{ bounds.max.x, bounds.max.y, bounds.min.z },

		{ bounds.min.x, bounds.min.y, bounds.max.z },
		{ bounds.max.x, bounds.min.y, bounds.max.z },
		{ bounds.min.x, bounds.max.y, bounds.max.z },
		{ bounds.max.x, bounds.max.y, bounds.max.z }
	};

	int screenW = GetScreenWidth();
	int screenH = GetScreenHeight();

	float minX = FLT_MAX;
	float minY = FLT_MAX;
	float maxX = -FLT_MAX;
	float maxY = -FLT_MAX;

	Vector3 forward = NormalizeSafe(Vector3Subtract(cam.target, cam.position));

	bool hasPointInFront = false;

	for (int i = 0; i < 8; i++)
	{
		Vector3 toPoint = Vector3Subtract(corners[i], cam.position);
		float d = Vector3DotProduct(toPoint, forward);

		if (d > 0.05f)
			hasPointInFront = true;

		Vector2 screen = GetWorldToScreenEx(
			corners[i],
			cam,
			screenW,
			screenH
		);

		minX = fminf(minX, screen.x);
		minY = fminf(minY, screen.y);
		maxX = fmaxf(maxX, screen.x);
		maxY = fmaxf(maxY, screen.y);
	}

	if (!hasPointInFront)
		return false;

	Rectangle rawRect = {
		minX,
		minY,
		maxX - minX,
		maxY - minY
	};

	Rectangle clamped = ClampRectToScreen(rawRect, screenW, screenH);

	if (RectAreaSafe(clamped) <= 1.0f)
		return false;

	Vector3 center = {
		(bounds.min.x + bounds.max.x) * 0.5f,
		(bounds.min.y + bounds.max.y) * 0.5f,
		(bounds.min.z + bounds.max.z) * 0.5f
	};

	outDepth = Vector3DotProduct(
		Vector3Subtract(center, cam.position),
		forward
	);

	outRect = clamped;
	return outDepth > 0.05f;
}

void Game::BuildScenePropOccluders(
	const Camera3D& cam,
	const std::vector<int>& candidateIndices
)
{
	screenOccluders.clear();

	debugOccluderRejectedNoModel = 0;
	debugOccluderRejectedNotMarked = 0;
	debugOccluderRejectedTransparent = 0;
	debugOccluderRejectedNoRect = 0;
	debugOccluderRejectedSmall = 0;

	for (int propIndex : candidateIndices)
	{
		if (propIndex < 0 || propIndex >= (int)sceneProps.size())
			continue;

		const SceneProp& prop = sceneProps[propIndex];

		if (!prop.visible || prop.model == nullptr)
		{
			debugOccluderRejectedNoModel++;
			continue;
		}

		if (!prop.canOcclude)
		{
			debugOccluderRejectedNotMarked++;
			continue;
		}

		Rectangle rect{};
		float depth = 0.0f;

		if (!GetScenePropScreenRect(propIndex, cam, rect, depth))
		{
			debugOccluderRejectedNoRect++;
			continue;
		}

		BoundingBox worldBounds{};

		if (!GetScenePropRenderBoundsWorld(prop, worldBounds))
		{
			debugOccluderRejectedNoRect++;
			continue;
		}

		ScreenOccluder occluder{};
		occluder.propIndex = propIndex;
		occluder.rect = rect;
		occluder.depth = depth;
		occluder.worldBounds = worldBounds;

		screenOccluders.push_back(occluder);
	}

	std::sort(
		screenOccluders.begin(),
		screenOccluders.end(),
		[](const ScreenOccluder& a, const ScreenOccluder& b)
		{
			return a.depth < b.depth;
		}
	);
}

bool Game::IsScenePropOccludedByScreenOccluders(
	int propIndex,
	const Camera3D& cam
) const
{
	if (!enableOcclusionCulling)
		return false;

	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return false;

	const SceneProp& prop = sceneProps[propIndex];

	if (prop.ignoreOcclusionCulling)
		return false;

	BoundingBox targetBounds{};

	if (!GetScenePropRenderBoundsWorld(prop, targetBounds))
		return false;

	Vector3 targetCenter = {
		(targetBounds.min.x + targetBounds.max.x) * 0.5f,
		(targetBounds.min.y + targetBounds.max.y) * 0.5f,
		(targetBounds.min.z + targetBounds.max.z) * 0.5f
	};

	Vector3 toTarget = Vector3Subtract(targetCenter, cam.position);
	float targetDistance = Vector3Length(toTarget);

	if (targetDistance <= 0.05f)
		return false;

	Vector3 rayDir = Vector3Scale(toTarget, 1.0f / targetDistance);

	Ray ray{};
	ray.position = cam.position;
	ray.direction = rayDir;

	Vector2 targetScreen = GetWorldToScreenEx(
		targetCenter,
		cam,
		GetScreenWidth(),
		GetScreenHeight()
	);

	for (const ScreenOccluder& occluder : screenOccluders)
	{
		if (occluder.propIndex == propIndex)
			continue;

		// First cheap test:
		// The target center must be inside the occluder's screen rectangle.
		Rectangle safeRect = occluder.rect;

		// Shrink occluder rect slightly to avoid edge false positives.
		float shrinkX = safeRect.width * 0.12f;
		float shrinkY = safeRect.height * 0.12f;

		safeRect.x += shrinkX;
		safeRect.y += shrinkY;
		safeRect.width -= shrinkX * 2.0f;
		safeRect.height -= shrinkY * 2.0f;

		if (safeRect.width <= 2.0f || safeRect.height <= 2.0f)
			continue;

		if (!CheckCollisionPointRec(targetScreen, safeRect))
			continue;

		// Second real test:
		// A ray from camera to target center must hit the occluder first.
		RayCollision hit = GetRayCollisionBox(ray, occluder.worldBounds);

		if (!hit.hit)
			continue;

		if (hit.distance < targetDistance - occlusionDepthBias)
			return true;
	}

	return false;
}
void Game::DrawOutlineSettingsUI()
{
	if (!ImGui::CollapsingHeader("Pickup Outline", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	ImGui::Text("Visible Outline");

	if (DrawRaylibColorEdit4("Visible Outline Color", pickupOutlineColor))
	{
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat("Visible Outline Width", &pickupOutlineWidth, 0.1f, 0.0f, 30.0f))
	{
	}
	PushUndoIfItemActivated();

	ImGui::Separator();

	if (ImGui::Checkbox("Pulse Visible Outline", &pickupOutlinePulseEnabled))
	{
	}
	PushUndoIfItemActivated();

	if (pickupOutlinePulseEnabled)
	{
		if (ImGui::DragFloat("Pulse Min Width", &pickupOutlinePulseMinWidth, 0.1f, 0.0f, 30.0f))
		{
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat("Pulse Max Width", &pickupOutlinePulseMaxWidth, 0.1f, 0.0f, 30.0f))
		{
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat("Pulse Speed", &pickupOutlinePulseSpeed, 0.05f, 0.0f, 20.0f))
		{
		}
		PushUndoIfItemActivated();

		if (ImGui::SliderInt("Pulse Min Alpha", &pickupOutlinePulseMinAlpha, 0, 255))
		{
		}
		PushUndoIfItemActivated();

		if (ImGui::SliderInt("Pulse Max Alpha", &pickupOutlinePulseMaxAlpha, 0, 255))
		{
		}
		PushUndoIfItemActivated();
	}

	ImGui::Separator();
	ImGui::Text("Occluded Outline");

	if (ImGui::Checkbox("Draw Occluded Outline Through Objects", &pickupOccludedOutlineEnabled))
	{
	}
	PushUndoIfItemActivated();

	if (pickupOccludedOutlineEnabled)
	{
		if (ImGui::DragFloat("Occluded Outline Width", &pickupOccludedOutlineWidth, 0.1f, 0.0f, 30.0f))
		{
		}
		PushUndoIfItemActivated();

		if (DrawRaylibColorEdit4("Occluded Outline Color", pickupOccludedOutlineColor))
		{
		}
		PushUndoIfItemActivated();
	}

	ImGui::Separator();
	ImGui::Text("Hidden Fill");

	if (ImGui::Checkbox("Draw Hidden Fill Through Objects", &pickupHiddenFillEnabled))
	{
	}
	PushUndoIfItemActivated();

	if (pickupHiddenFillEnabled)
	{
		if (ImGui::DragFloat("Hidden Fill Width", &pickupHiddenFillWidth, 0.1f, 0.0f, 30.0f))
		{
		}
		PushUndoIfItemActivated();

		if (DrawRaylibColorEdit4("Hidden Fill Color", pickupHiddenFillColor))
		{
		}
		PushUndoIfItemActivated();

		ImGui::TextDisabled("Fill stays static. Pulse affects visible outline only.");
	}
}

void Game::DrawInteractableOutline(const Camera3D& cam) const
{
	if (targetedScenePropIndex < 0) return;
	if (hasHeldBody) return;
	if (editMode) return;
	if (inspectMode) return;

	DrawScenePropOutlineByIndex(targetedScenePropIndex, cam);
}
void Game::UpdateGameplay(float dt)
{
	double frameStart = GetTime();
	targetedScenePropIndex = -1;

	HandleUndoRedo();
	const bool showEditorUI = editMode || cursorUnlocked;

	ImGuiIO& io = ImGui::GetIO();
	bool wantsKeyboard = showEditorUI ? io.WantCaptureKeyboard : false;
	bool wantsMouse = showEditorUI ? io.WantCaptureMouse : false;
	if (editMode)
	{
		UpdateEditCamera(dt);
	}
	else
	{
#if GAME_ENABLE_EDITOR

		if (IsKeyPressed(KEY_TAB) && !inspectMode && !dialogueActive)
		{
			ToggleCursorMode();
		}
#endif
		// Toggle inspect FIRST
// Toggle inspect FIRST
		if (hasHeldBody && IsKeyPressed(KEY_F))
		{
			if (inspectMode) ExitInspectMode();
			else EnterInspectMode();
		}

		// Must run every gameplay frame, including the frames AFTER inspect exits,
		// otherwise the blackout fade-out cannot update.
		UpdateMusicBoxInspectEasterEgg(dt);

		// If inspecting, stop normal gameplay update here
		if (inspectMode)
		{
			UpdateInspectMode(dt);
			if (hasHeldBody)
			{
				if (heldItemScanState != HeldItemScanState::None)
				{
					UpdateHeldItemScan(dt);
				}
				else
				{
					UpdateHeldBody();
				}
			}

			UpdateDialogue();

			physics->Step(dt);
			SyncScenePropsFromPhysics();
			updatePBRShader();
			return;
		}

		/*
		*
		* 		bool playerInputLocked =
			dialogueActive ||
			openingCinematicActive;

		bool customerDialogueLookActive =
			dialogueActive &&
			activeDialogueCustomerIndex >= 0 &&
			customerDialogueCameraLock;
		*/


		bool cinematicActive =
			IsCinematicCameraActive() ||
			dayIntroActive;

		bool customerDialogueMovementLocked =
			customerDialogueFocusActive &&
			dialogueActive &&
			activeDialogueCustomerIndex >= 0;

		if (!cursorUnlocked && !wantsKeyboard)
		{
			if (cinematicActive)
			{
				StopPlayerMovementForDialogue();
			}
			else if (customerDialogueMovementLocked)
			{
				// Read mouse look input, but cancel walking movement.
				UpdateCustomerDialogueLookInputOnly();
			}
			else
			{
				if (playerUsesJolt)
				{
					UpdateVirtualPlayer(dt);

					ResolvePlayerCustomerSlide();

					HandleBoxInteraction();
				}
				else
				{
					player.update(dt);
					ResolvePlayerCollisions();
					HandleBoxInteraction();
				}
			}
		}

		/*
				else if (playerInputLocked)
		{
			StopPlayerMovementForDialogue();
		}
		*/


		if (!cursorUnlocked && !wantsMouse)
		{
			if (openingCinematicActive)
			{
				UpdateOpeningCinematic(dt);
			}
			else if (cinematicCamera.active)
			{
				UpdateCinematicCamera(dt);
			}
			else
			{
				updateCameraController(dt);

				if (customerDialogueFocusActive)
				{
					UpdateCustomerDialogueFocus(dt);
				}

				updateCameraFPS();
			}
		}

		UpdateCinematicTriggers(dt);
		UpdateLookSoundTriggers(dt);
	}

	if (hasHeldBody)
	{
		if (heldItemScanState != HeldItemScanState::None)
		{
			UpdateHeldItemScan(dt);
		}
		else
		{
			UpdateHeldBody();
		}
	}

	if (pendingPostLoadPhysicsRestore)
	{
		ApplyScenePropPhysicsModesAfterRebuild();
		pendingPostLoadPhysicsRestore = false;

		SyncScenePropsFromPhysics();
		updatePBRShader();
		return;
	}

	if (IsKeyPressed(KEY_I))
	{
		for (Customer& customer : customers)
			customer.SetAnimState(CustomerAnimState::Idle);
	}

	if (IsKeyPressed(KEY_O))
	{
		for (Customer& customer : customers)
			customer.SetAnimState(CustomerAnimState::Walking);
	}

	if (IsKeyPressed(KEY_F3))
	{
		showPerfOverlay = !showPerfOverlay;
	}

	/*
		if (IsKeyPressed(KEY_ONE))
	{
		lights[2].enabled = !lights[2].enabled;
		MarkShadowMapsDirty();
	}

	if (IsKeyPressed(KEY_TWO))
	{
		lights[1].enabled = !lights[1].enabled;
		MarkShadowMapsDirty();
	}

	if (IsKeyPressed(KEY_THREE))
	{
		lights[3].enabled = !lights[3].enabled;
		MarkShadowMapsDirty();
	}

	if (IsKeyPressed(KEY_FIVE))
	{
		lights[4].enabled = !lights[4].enabled;
		MarkShadowMapsDirty();
	}
	if (IsKeyPressed(KEY_FOUR))
	{
		lights[0].enabled = !lights[0].enabled;
		MarkShadowMapsDirty();
	}
	*/


	double t = GetTime();

	UpdateCustomerSpawner(dt);

	ProcessPendingCustomerBodyCreates();

	UpdateCustomerPOINavigation(dt);
	UpdateCustomerDynamicAvoidance(dt);
	UpdateCustomersSafely(dt);

	// Force the active dialogue customer to stay stopped
	// after normal customer AI has run.
	FreezeActiveDialogueCustomer();

	perf.customerAIUpdateMs = MsSince(t);

	t = GetTime();
	SyncCustomerBodiesToCustomers(dt);
	perf.customerBodySyncMs = MsSince(t);

	t = GetTime();
	DespawnPendingCustomers();
	SyncScenePropsFromPhysics();
	perf.scenePhysicsSyncMs = MsSince(t);

	t = GetTime();

	UpdateInteractableTarget();
	UpdateHeldPlacementPreview();
	HandleStoreControlInteraction();

	perf.interactTargetMs = MsSince(t);

	t = GetTime();
	physics->Step(dt);
	perf.physicsMs = MsSince(t);

	t = GetTime();
	UpdateDialogue();
	perf.dialogueUpdateMs = MsSince(t);

	if (pendingDayIntroAfterOpeningCinematic &&
		!openingCinematicActive &&
		!dialogueActive)
	{
		pendingDayIntroAfterOpeningCinematic = false;
		StartDayIntro();
	}

	perf.pbrUpdateMs = 0.0f;

	perf.updateMs = MsSince(frameStart);
	UpdateStoreDayState(dt);
	UpdateDayIntro(dt);
}
void Game::update()
{
	float dt = GetFrameTime();

	if (IsMainMenuMusicScreen(currentScreen))
	{
		UpdateMainMenuMusic(dt);
	}
	if (startNewGameRequested)
	{
		startNewGameRequested = false;
		StartNewGame();
		return;
	}

	switch (currentScreen)
	{
	case GameScreen::Loading:
		UpdateLoading();
		break;

	case GameScreen::MainMenu:
		// Main menu buttons are handled in DrawMainMenu().
		// No gameplay/physics update here.
		break;

	case GameScreen::Controls:
		if (IsKeyPressed(KEY_LEFT_ALT))
		{
			SetScreen(menuReturnScreen);
		}
		break;

	case GameScreen::AudioSettings:
		if (IsKeyPressed(KEY_LEFT_ALT))
		{
			SetScreen(menuReturnScreen);
		}
		break;
	case GameScreen::Credits:
		if (IsKeyPressed(KEY_LEFT_ALT))
		{
			SetScreen(menuReturnScreen);
		}
		break;
	case GameScreen::GraphicsSettings:
		if (IsKeyPressed(KEY_LEFT_ALT))
		{
			SetScreen(menuReturnScreen);
		}
		break;

	case GameScreen::Playing:
#if GAME_ENABLE_EDITOR
		if (IsKeyPressed(KEY_B))
		{
			ShowCampaignCompleteScreen(true);
			return;
		}
#endif

		if (campaignCompleteScreenVisible)
		{
			UpdateCampaignCompleteScreen();
			return;
		}

		if (IsKeyPressed(KEY_P))
		{
			SetScreen(GameScreen::Paused);
			return;
		}

		UpdateGameplay(dt);
		break;

	case GameScreen::Paused:
		if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_LEFT_ALT))
		{
			ResumeGame();
			return;
		}

		// Do not call UpdateGameplay() here.
		// This keeps physics, player movement, customers, and dialogue frozen.
		break;
	}

	UpdateRecordPlayer(dt);

}


void Game::HandleStoreControlInteraction()
{
	int storeControlIndex = FindLookedAtStoreControlProp();

	if (storeControlIndex < 0 ||
		storeControlIndex >= (int)sceneProps.size())
	{
		return;
	}

	targetedScenePropIndex = storeControlIndex;

	SceneProp& prop = sceneProps[storeControlIndex];

	if (prop.model != nullptr)
	{
		BuildOutlineModelCache(prop.model);
	}

	if (!IsKeyPressed(KEY_E))
		return;

	if (IsStoreOpenControlProp(prop))
	{
		if (storeDayState == StoreDayState::Closed ||
			storeDayState == StoreDayState::Results)
		{
			OpenStore();
		}

		return;
	}

	if (IsStoreCloseControlProp(prop))
	{
		if (storeDayState == StoreDayState::Open)
		{
			BeginStoreClosing("Closed manually.");
		}

		return;
	}
}

void Game::UpdateInteractableTarget()
{
	targetedScenePropIndex = -1;

	if (editMode) return;
	if (cursorUnlocked) return;
	if (inspectMode) return;
	if (hasHeldBody) return;

	InteractHit hit = FindInteractableBodyRaycast();

	if (hit.valid && hit.scenePropIndex >= 0)
	{
		targetedScenePropIndex = hit.scenePropIndex;

		SceneProp& prop = sceneProps[targetedScenePropIndex];

		if (prop.model != nullptr)
		{
			BuildOutlineModelCache(prop.model);
		}
	}
}
void Game::BuildCustomerTypes()
{
	customerTypes.clear();

	// =========================================================
	// TYPE 1: Seller customer
	// Uses seller_part1 dialogue.
	// =========================================================

	CustomerType seller;
	seller.id = "seller_male";
	seller.modelPath = "Models/ExiaGundam.glb";
	seller.dialogueScriptId = "seller_part1";

	seller.renderScale = { 1.0f, 1.0f, 1.0f };
	seller.renderRotationAxis = { 0.0f, 1.0f, 0.0f };
	seller.renderRotationAngleDeg = 0.0f;

	seller.role = CustomerRole::Seller;
	seller.poiGroup = "seller";

	seller.animSet.animations =
		LoadModelAnimations(
			seller.modelPath.c_str(),
			&seller.animSet.animationCount
		);

	seller.animSet.idleIndex = FindAnimationIndexByName(seller.animSet, "Idle");
	seller.animSet.walkIndex = FindAnimationIndexByName(seller.animSet, "Walking");
	seller.animSet.emoteIndex = FindAnimationIndexByName(seller.animSet, "Emote");
	seller.animSet.pointIndex = FindAnimationIndexByName(seller.animSet, "Point");
	seller.animSet.leftHandIndex = FindAnimationIndexByName(seller.animSet, "LeftHand");
	seller.animSet.leftLookIndex = FindAnimationIndexByName(seller.animSet, "LookLeft");
	seller.animSet.thinkIndex = FindAnimationIndexByName(seller.animSet, "Think");
	seller.animSet.giveIndex = FindAnimationIndexByName(seller.animSet, "Give");
	seller.animSet.danceIndex = FindAnimationIndexByName(seller.animSet, "Dance");
	seller.animSet.twerkIndex = FindAnimationIndexByName(seller.animSet, "Twerk");

	seller.pbrMetallicValue = 0.08f;
	seller.pbrRoughnessValue = 0.25f;
	seller.pbrRoughnessScale = 0.45f;
	seller.pbrReflectionStrength = 0.35f;
	seller.pbrEmissivePower = 3.0f;
	seller.moveSpeed = 0.65;


	customerTypes[seller.id] = seller;


	// =========================================================
	// TYPE 2: Browser customer
	// For now, it can use the same model/animations,
	// but it starts with browser_customer dialogue.
	// Later, replace modelPath with a different GLB.
	// =========================================================

	CustomerType browser;
	browser.role = CustomerRole::Browser;
	browser.id = "browser_male";
	browser.modelPath = "Models/japanmanaAnim.glb";
	browser.dialogueScriptId = "browser_customer";

	browser.animSet.animations =
		LoadModelAnimations(
			browser.modelPath.c_str(),
			&browser.animSet.animationCount
		);

	browser.poiGroup = "browser";

	browser.animSet.idleIndex = FindAnimationIndexByName(browser.animSet, "Idle");
	browser.animSet.walkIndex = FindAnimationIndexByName(browser.animSet, "Walking");
	browser.animSet.emoteIndex = FindAnimationIndexByName(browser.animSet, "Emote");
	browser.animSet.pointIndex = FindAnimationIndexByName(browser.animSet, "Point");
	browser.animSet.leftHandIndex = FindAnimationIndexByName(browser.animSet, "LeftHand");
	browser.animSet.leftLookIndex = FindAnimationIndexByName(browser.animSet, "LookLeft");
	browser.animSet.thinkIndex = FindAnimationIndexByName(browser.animSet, "Think");
	browser.animSet.giveIndex = FindAnimationIndexByName(browser.animSet, "Give");
	browser.animSet.danceIndex = FindAnimationIndexByName(browser.animSet, "Dance");
	browser.animSet.twerkIndex = FindAnimationIndexByName(browser.animSet, "Twerk");

	browser.moveSpeed = 0.55f;

	customerTypes[browser.id] = browser;

	CustomerType newCustomer;
	newCustomer.id = "new_customer";
	newCustomer.modelPath = "Models/GmanAnim.glb";
	newCustomer.dialogueScriptId = "seller_part1";

	newCustomer.animSet.animations =
		LoadModelAnimations(
			newCustomer.modelPath.c_str(),
			&newCustomer.animSet.animationCount
		);
	newCustomer.role = CustomerRole::Seller;
	newCustomer.poiGroup = "seller";
	newCustomer.animSet.idleIndex = FindAnimationIndexByName(newCustomer.animSet, "Idle");
	newCustomer.animSet.walkIndex = FindAnimationIndexByName(newCustomer.animSet, "Walking");
	newCustomer.animSet.emoteIndex = FindAnimationIndexByName(newCustomer.animSet, "Emote");
	newCustomer.animSet.pointIndex = FindAnimationIndexByName(newCustomer.animSet, "Point");
	newCustomer.animSet.leftHandIndex = FindAnimationIndexByName(newCustomer.animSet, "LeftHand");
	newCustomer.animSet.leftLookIndex = FindAnimationIndexByName(newCustomer.animSet, "LookLeft");
	newCustomer.animSet.thinkIndex = FindAnimationIndexByName(newCustomer.animSet, "Think");
	newCustomer.animSet.giveIndex = FindAnimationIndexByName(newCustomer.animSet, "Give");
	newCustomer.animSet.danceIndex = FindAnimationIndexByName(newCustomer.animSet, "Dance");
	newCustomer.animSet.twerkIndex = FindAnimationIndexByName(newCustomer.animSet, "Twerk");
	newCustomer.renderScale = { 0.006f, 0.006f, 0.006f };
	newCustomer.renderRotationAxis = { 1.0f, 0.0f, 0.0f };
	newCustomer.renderRotationAngleDeg = 90.0f;

	newCustomer.moveSpeed = 1.5f;
	customerTypes[newCustomer.id] = newCustomer;
}

void Game::UpdateCustomers(float dt)
{
	for (Customer& customer : customers)
	{
		customer.Update(dt);
	}
}

void Game::cameraPlayerPos()
{
	camera.position = player.m_pos + Vector3{ 0, 1.7f, 0 }; // eye height

	Vector3 forward = {
		cosf(player.pitch * DEG2RAD) * sinf(player.yaw * DEG2RAD),
		sinf(player.pitch * DEG2RAD),
		cosf(player.pitch * DEG2RAD) * cosf(player.yaw * DEG2RAD)
	};

	camera.target = camera.position + forward;
}



void Game::DrawLevel(void) const
{
	Vector2 zeroOffset = { 0.0f, 0.0f };

	// ----- FLOOR -----
	SetShaderValue(pbrShader, textureTilingLoc, &floorTextureTiling, SHADER_UNIFORM_VEC2);
	SetShaderValue(pbrShader, metallicValueLoc, &pbrFloor.materials[0].maps[MATERIAL_MAP_METALNESS].value, SHADER_UNIFORM_FLOAT);
	SetShaderValue(pbrShader, roughnessValueLoc, &pbrFloor.materials[0].maps[MATERIAL_MAP_ROUGHNESS].value, SHADER_UNIFORM_FLOAT);

	float floorAO = pbrFloor.materials[0].maps[MATERIAL_MAP_OCCLUSION].value;
	float floorNormalStrength = 1.0f;
	float floorEmissiveIntensity = 0.0f;

	SetShaderValue(pbrShader, offsetLoc, &zeroOffset, SHADER_UNIFORM_VEC2);
	SetShaderValue(pbrShader, aoValueLoc, &floorAO, SHADER_UNIFORM_FLOAT);
	SetShaderValue(pbrShader, normalValueLoc, &floorNormalStrength, SHADER_UNIFORM_FLOAT);
	SetShaderValue(pbrShader, emissiveIntensityLoc, &floorEmissiveIntensity, SHADER_UNIFORM_FLOAT);

	Vector4 floorAlbedoColor = ColorNormalize(pbrFloor.materials[0].maps[MATERIAL_MAP_ALBEDO].color);
	Vector4 floorEmissiveColor = ColorNormalize(pbrFloor.materials[0].maps[MATERIAL_MAP_EMISSION].color);

	SetShaderValue(pbrShader, albedoColorLoc, &floorAlbedoColor, SHADER_UNIFORM_VEC4);
	SetShaderValue(pbrShader, emissiveColorLoc, &floorEmissiveColor, SHADER_UNIFORM_VEC4);

	int useTexAlbedo = 1;
	int useTexNormal = 1;
	int useTexMRA = 1;
	int useTexEmissive = 0;

	SetShaderValueIfValid(pbrShader, pbrUseTexAlbedoLoc, &useTexAlbedo, SHADER_UNIFORM_INT);
	SetShaderValueIfValid(pbrShader, pbrUseTexNormalLoc, &useTexNormal, SHADER_UNIFORM_INT);
	SetShaderValueIfValid(pbrShader, pbrUseTexMRALoc, &useTexMRA, SHADER_UNIFORM_INT);
	SetShaderValueIfValid(pbrShader, pbrUseTexEmissiveLoc, &useTexEmissive, SHADER_UNIFORM_INT);


	// Floor should receive shadows.
	int receiveShadows = 1;
	SetShaderValueIfValid(
		pbrShader,
		pbrReceiveShadowsLoc,
		&receiveShadows,
		SHADER_UNIFORM_INT
	);

	int alphaMode = 0;
	float alphaCutoff = 0.5f;

	SetShaderValueIfValid(
		pbrShader,
		pbrAlphaModeLoc,
		&alphaMode,
		SHADER_UNIFORM_INT
	);

	SetShaderValueIfValid(
		pbrShader,
		pbrAlphaCutoffLoc,
		&alphaCutoff,
		SHADER_UNIFORM_FLOAT
	);

	int useGltfMetallicRoughness = 0;
	SetShaderValueIfValid(
		pbrShader,
		pbrUseGltfMetallicRoughnessLoc,
		&useGltfMetallicRoughness,
		SHADER_UNIFORM_INT
	);
	DrawModelEuler(
		pbrFloor,
		floorDrawPos,
		floorDrawRotDeg,
		{ 1.3f, 1.0f, 1.3f },
		WHITE
	);


	/*
	// ----- PROP -----
	SetShaderValue(pbrShader, textureTilingLoc, &propTextureTiling, SHADER_UNIFORM_VEC2);
	SetShaderValue(pbrShader, metallicValueLoc, &testProp.materials[0].maps[MATERIAL_MAP_METALNESS].value, SHADER_UNIFORM_FLOAT);
	SetShaderValue(pbrShader, roughnessValueLoc, &testProp.materials[0].maps[MATERIAL_MAP_ROUGHNESS].value, SHADER_UNIFORM_FLOAT);

	float propAO = testProp.materials[0].maps[MATERIAL_MAP_OCCLUSION].value;
	float propNormalStrength = 1.0f;
	float propEmissiveIntensity = 0.0f;

	SetShaderValue(pbrShader, offsetLoc, &zeroOffset, SHADER_UNIFORM_VEC2);
	SetShaderValue(pbrShader, aoValueLoc, &propAO, SHADER_UNIFORM_FLOAT);
	SetShaderValue(pbrShader, normalValueLoc, &propNormalStrength, SHADER_UNIFORM_FLOAT);
	SetShaderValue(pbrShader, emissiveIntensityLoc, &propEmissiveIntensity, SHADER_UNIFORM_FLOAT);

	Vector4 propAlbedoColor = ColorNormalize(testProp.materials[0].maps[MATERIAL_MAP_ALBEDO].color);
	Vector4 propEmissiveColor = ColorNormalize(testProp.materials[0].maps[MATERIAL_MAP_EMISSION].color);

	SetShaderValue(pbrShader, albedoColorLoc, &propAlbedoColor, SHADER_UNIFORM_VEC4);
	SetShaderValue(pbrShader, emissiveColorLoc, &propEmissiveColor, SHADER_UNIFORM_VEC4);




	SetTextureUsage(pbrShader, 0, 0, 0, 0);
	DrawModelEuler(testProp, propDrawPos, propDrawRotDeg, { 1.0f, 1.0f, 1.0f }, WHITE);
	*/


	//gacha


		//temp
	SetShaderValue(blurShader, blurScaleLoc, &inspectBlurScale, SHADER_UNIFORM_FLOAT);	//temp
	SetShaderValue(blurShader, blurScaleLoc, &inspectBlurScale, SHADER_UNIFORM_FLOAT);

	for (int i = 0; i < lightCount; i++)
	{
		Color c = {
			(unsigned char)(lights[i].color[0] * 255),
			(unsigned char)(lights[i].color[1] * 255),
			(unsigned char)(lights[i].color[2] * 255),
			255
		};

		if (lights[i].enabled && editMode)
			DrawSphereEx(lights[i].position, 0.15f, 8, 8, c);
	}
	/*
	const int floorExtent = 25;
	const float tileSize = 5.0f;
	const Color tileColor1 = Color{ 150, 200, 200, 255 };

	// Floor tiles
	for (int y = -floorExtent; y < floorExtent; y++)
	{
		for (int x = -floorExtent; x < floorExtent; x++)
		{
			if ((y & 1) && (x & 1))
			{
				DrawPlane(Vector3 { x* tileSize, 0.0f, y* tileSize }, Vector2{ tileSize, tileSize }, tileColor1);
			}
			else if (!(y & 1) && !(x & 1))
			{
				DrawPlane(Vector3 { x* tileSize, 0.0f, y* tileSize }, Vector2{ tileSize, tileSize }, LIGHTGRAY);
			}
		}
	}

	const Vector3 towerSize = Vector3{ 16.0f, 32.0f, 16.0f };
	const Color towerColor = Color{ 150, 200, 200, 255 };

	Vector3 towerPos = Vector3{ 16.0f, 16.0f, 16.0f };
	DrawCubeV(towerPos, towerSize, towerColor);
	DrawCubeWiresV(towerPos, towerSize, DARKBLUE);

	towerPos.x *= -1;
	DrawCubeV(towerPos, towerSize, towerColor);
	DrawCubeWiresV(towerPos, towerSize, DARKBLUE);

	towerPos.z *= -1;
	DrawCubeV(towerPos, towerSize, towerColor);
	DrawCubeWiresV(towerPos, towerSize, DARKBLUE);

	towerPos.x *= -1;
	DrawCubeV(towerPos, towerSize, towerColor);
	DrawCubeWiresV(towerPos, towerSize, DARKBLUE);

	// Red sun
	DrawSphere(Vector3{ 300.0f, 300.0f, 0.0f }, 100.0f, Color{ 255, 0, 0, 255 });
	*/

}

void Game::updateCameraFPS()
{
	const Vector3 up = { 0.0f, 1.0f, 0.0f };
	const Vector3 targetOffset = { 0.0f, 0.0f, -1.0f };

	Vector3 yawVec = Vector3RotateByAxisAngle(targetOffset, up, player.yaw);

	float maxAngleUp = Vector3Angle(up, yawVec);
	maxAngleUp -= 0.001f;
	if (-(player.pitch) > maxAngleUp) player.pitch = -maxAngleUp;

	float maxAngleDown = Vector3Angle(Vector3Negate(up), yawVec);
	maxAngleDown *= -1.0f;
	maxAngleDown += 0.001f;
	if (-(player.pitch) < maxAngleDown) player.pitch = -maxAngleDown;

	Vector3 right = Vector3Normalize(Vector3CrossProduct(yawVec, up));

	float pitchAngle = -player.pitch - lean.y;
	pitchAngle = Clamp(pitchAngle, -PI / 2 + 0.0001f, PI / 2 - 0.0001f);
	Vector3 pitchVec = Vector3RotateByAxisAngle(yawVec, right, pitchAngle);

	float headSin = sinf(headTimer * PI);
	float headCos = cosf(headTimer * PI);
	const float stepRotation = 0.01f;

	camera.up = Vector3RotateByAxisAngle(up, pitchVec, headSin * stepRotation + lean.x);

	const float bobSide = 0.1f;
	const float bobUp = 0.15f;
	Vector3 bobbing = Vector3Scale(right, headSin * bobSide);
	bobbing.y = fabsf(headCos * bobUp);

	camera.position = Vector3Add(camera.position, Vector3Scale(bobbing, walkLerp));
	camera.target = Vector3Add(camera.position, pitchVec);
}

void Game::updateCameraController(float dt)
{
	headLerp = Lerp(headLerp, (player.m_crouching ? CROUCH_HEIGHT : STAND_HEIGHT), 20.0f * dt);

	camera.position = {
		player.m_pos.x,
		player.m_pos.y + (BOTTOM_HEIGHT + headLerp),
		player.m_pos.z
	};

	if (player.m_isGrounded && ((player.m_forward != 0) || (player.m_sideway != 0)))
	{
		headTimer += dt * 3.0f;
		walkLerp = Lerp(walkLerp, 1.0f, 10.0f * dt);
		camera.fovy = Lerp(camera.fovy, 55.0f, 5.0f * dt);
	}
	else
	{
		walkLerp = Lerp(walkLerp, 0.0f, 10.0f * dt);
		camera.fovy = Lerp(camera.fovy, 60.0f, 5.0f * dt);
	}

	lean.x = Lerp(lean.x, player.m_sideway * 0.02f, 10.0f * dt);
	lean.y = Lerp(lean.y, player.m_forward * 0.015f, 10.0f * dt);
}

static bool IsRealTexture(Texture2D texture)
{
	// raylib default texture often appears as id 1 in your logs.
	// Your real loaded textures are higher IDs.
	return texture.id > 1;
}

void Game::RepairAnimatedCustomerMaterialTextures(Model& model)
{
	bool modelHasAnyNormal = false;
	bool modelHasAnyMR = false;

	for (int i = 0; i < model.materialCount; i++)
	{
		Material& mat = model.materials[i];

		if (IsRealTexture(mat.maps[MATERIAL_MAP_NORMAL].texture))
		{
			modelHasAnyNormal = true;
		}

		if (IsRealTexture(mat.maps[MATERIAL_MAP_METALNESS].texture))
		{
			modelHasAnyMR = true;
		}
	}

	for (int i = 0; i < model.materialCount; i++)
	{
		Material& mat = model.materials[i];

		if (modelHasAnyNormal &&
			!IsRealTexture(mat.maps[MATERIAL_MAP_NORMAL].texture))
		{
			mat.maps[MATERIAL_MAP_NORMAL].texture = neutralNormalTexture;
		}

		if (modelHasAnyMR &&
			!IsRealTexture(mat.maps[MATERIAL_MAP_METALNESS].texture))
		{
			mat.maps[MATERIAL_MAP_METALNESS].texture = defaultGltfMRTexture;
		}
	}
}

void Game::ApplyAnimatedCustomerMaterial(Model& model)
{
	for (int i = 0; i < model.meshCount; i++)
	{
		if (model.meshes[i].tangents == nullptr)
		{
			GenMeshTangents(&model.meshes[i]);
		}
	}

	for (int i = 0; i < model.materialCount; i++)
	{
		model.materials[i].shader = animatedPbrShader;
	}

	if (shadowCasters[0].shadowMap.texture.id != 0)
	{
		AttachShadowTextureToModel(model, shadowCasters[0].shadowMap.texture);
	}
}
Customer* Game::SpawnCustomerOfType(
	const std::string& typeId,
	Vector3 position,
	CustomerAnimState startState
)
{
	auto it = customerTypes.find(typeId);

	if (it == customerTypes.end())
	{
		TraceLog(LOG_WARNING, "Customer type not found: %s", typeId.c_str());
		return nullptr;
	}

	CustomerType& type = it->second;

	if (type.animSet.animations == nullptr || type.animSet.animationCount <= 0)
	{
		TraceLog(LOG_WARNING, "Customer type has no animations: %s", typeId.c_str());
		return nullptr;
	}

	Model* modelPtr = AcquireCustomerModelFromPool(typeId);

	if (modelPtr == nullptr)
		return nullptr;

	customers.emplace_back(
		modelPtr,
		position,
		&type.animSet
	);

	Customer& customer = customers.back();

	customer.customerTypeId = typeId;

	customer.role = type.role;
	customer.poiGroup = type.poiGroup;
	customer.moveSpeed = type.moveSpeed;

	customer.dialogueScriptId = type.dialogueScriptId;

	customer.scale = type.renderScale;
	customer.drawRotationAxis = type.renderRotationAxis;
	customer.drawRotationAngleDeg = type.renderRotationAngleDeg;

	customer.pbrMetallicValue = type.pbrMetallicValue;
	customer.pbrRoughnessValue = type.pbrRoughnessValue;
	customer.pbrRoughnessScale = type.pbrRoughnessScale;
	customer.pbrReflectionStrength = type.pbrReflectionStrength;
	customer.pbrEmissivePower = type.pbrEmissivePower;

	customer.SetAnimState(startState);

	return &customer;
}

void Game::BuildCustomers()
{
	RemoveCustomerBodies();

	for (Customer& customer : customers)
	{
		ReleaseCustomerModelToPool(customer.model);
	}

	customers.clear();

	// Optional: leave empty and let the spawner create customers.
	// This avoids starting the game with overlapping debug customers.

	// If you still want initial test customers, spawn them here,
	// but they will now come from the pool.
	/*
	SpawnCustomerOfType(
		"seller_male",
		Vector3{ 0.0f, 0.0f, 0.0f },
		CustomerAnimState::Idle
	);

	SpawnCustomerOfType(
		"browser_male",
		Vector3{ -3.0f, 0.0f, -2.0f },
		CustomerAnimState::Idle
	);

	SpawnCustomerOfType(
		"new_customer",
		Vector3{ -2.0f, 0.0f, 0.0f },
		CustomerAnimState::Idle
	);
	*/
}

void Game::LoadUIFont()
{
	std::vector<int> codepoints;

	// Basic ASCII
	for (int c = 32; c <= 126; c++)
	{
		codepoints.push_back(c);
	}

	// Yen sign
	codepoints.push_back(0x00A5); // ¥

	// Fullwidth Yen sign
	codepoints.push_back(0xFFE5); // ￥

	// Optional Japanese punctuation
	codepoints.push_back(0x3001); // 、
	codepoints.push_back(0x3002); // 。
	codepoints.push_back(0x30FC); // ー

	uiFont = LoadFontEx(
		"Fonts/open-sans/OpenSans-Regular.ttf",
		32,
		codepoints.data(),
		(int)codepoints.size()
	);

	uiFontLoaded = uiFont.texture.id != 0;

	if (uiFontLoaded)
	{
		SetTextureFilter(uiFont.texture, TEXTURE_FILTER_BILINEAR);
	}
	else
	{
		TraceLog(LOG_WARNING, "Failed to load UI font.");
	}
}

void Game::importAssets()
{
	LoadSoundEffects();

	pbrFloor = LoadModel("Models/Japan3.glb");
	gBoy = LoadModel("Models/GameBoy5.glb");
	//TraceLog(LOG_INFO, "gBoy meshCount = %i, materialCount = %i", gBoy.meshCount, gBoy.materialCount);
	Shelf = LoadModel("Models/Shelf.glb");

	customerModel = LoadModel("Models/japanmanaAnim.glb");


	BoundingBox b = GetModelBoundingBox(customerModel);
	/*
	TraceLog(LOG_INFO,
		"Customer bounds min(%.3f %.3f %.3f), max(%.3f %.3f %.3f)",
		b.min.x, b.min.y, b.min.z,
		b.max.x, b.max.y, b.max.z
	);

	TraceLog(LOG_INFO,
		"Customer mesh[0] vertexCount = %i, triangleCount = %i",
		customerModel.meshes[0].vertexCount,
		customerModel.meshes[0].triangleCount
	);

	TraceLog(LOG_INFO,
		"Customer boneCount = %i",
		customerModel.boneCount
	);
	*/



	customerAnimSet.animations =
		LoadModelAnimations("Models/japanmanaAnim.glb", &customerAnimSet.animationCount);




	for (int i = 0; i < customerAnimSet.animationCount; i++)
	{

		LoadUIFont();

		bool valid = IsModelAnimationValid(customerModel, customerAnimSet.animations[i]);

		TraceLog(
			LOG_INFO,
			"Customer anim[%i] name=%s valid=%i keyframeCount=%i animBoneCount=%i",
			i,
			customerAnimSet.animations[i].name,
			valid,
			customerAnimSet.animations[i].keyframeCount,
			customerAnimSet.animations[i].boneCount
		);
	}

	// You must adjust these indexes based on your GLB animation order.
	customerAnimSet.emoteIndex = 0;
	customerAnimSet.idleIndex = 1;
	customerAnimSet.walkIndex = 2;

	TraceLog(LOG_INFO, "Animation count = %i", customerAnimSet.animationCount);

	for (int i = 0; i < customerAnimSet.animationCount; i++)
	{
		bool valid = IsModelAnimationValid(customerModel, customerAnimSet.animations[i]);
		TraceLog(LOG_INFO, "Anim %i valid = %i, frameCount = %i",
			i,
			valid ? 1 : 0,
			customerAnimSet.animations[i].keyframeCount);
	}

	shelfAlbedo = LoadTexture("Textures/oak_veneer_01_diff_1k.png");
	shelfNormal = LoadTexture("Textures/oak_veneer_01_nor_gl_1k.png");
	shelfMRA = LoadTexture("Textures/oak_veneer_01_MRA_1k.png");

	gBoyAlbedo = LoadTexture("Textures/Gameboy_low_Gameboy_BaseColor1.png");
	gBoyNormal = LoadTexture("Textures/Gameboy_low_Gameboy_Normal.png");
	gBoyMRA = LoadTexture("Textures/GameBoy_MRA.png");


	Gacha = LoadModel("Models/Gachamachine.glb");
	pbrShader = LoadShader("Shaders/pbr_tangent.vs", "Shaders/pbr_tangent.fs");

	// Tangent shader needs these extra links
	pbrShader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(pbrShader, "mvp");
	pbrShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(pbrShader, "matModel");
	pbrShader.locs[SHADER_LOC_VERTEX_TANGENT] = GetShaderLocation(pbrShader, "vertexTangent");

	// Same core PBR links as the basic example
	pbrShader.locs[SHADER_LOC_MAP_ALBEDO] = GetShaderLocation(pbrShader, "albedoMap");
	pbrShader.locs[SHADER_LOC_MAP_METALNESS] = GetShaderLocation(pbrShader, "mraMap");
	pbrShader.locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(pbrShader, "normalMap");
	pbrShader.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(pbrShader, "emissiveMap");

	pbrShader.locs[SHADER_LOC_MAP_ROUGHNESS] =
		GetShaderLocation(pbrShader, "roughnessMap");

	pbrShader.locs[SHADER_LOC_MAP_OCCLUSION] =
		GetShaderLocation(pbrShader, "occlusionMap");

	pbrShader.locs[SHADER_LOC_MAP_HEIGHT] =
		GetShaderLocation(pbrShader, "shadowMap0");


	pbrAmbientColorLoc = GetShaderLocation(pbrShader, "ambientColor");
	pbrAmbientLoc = GetShaderLocation(pbrShader, "ambient");

	pbrShader.locs[SHADER_LOC_MAP_CUBEMAP] = GetShaderLocation(pbrShader, "environmentMap");
	reflectionStrengthLoc = GetShaderLocation(pbrShader, "reflectionStrength");

	pbrShader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(pbrShader, "viewPos");
	pbrShader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(pbrShader, "albedoColor");

	CacheShadowUniformLocations(pbrShader, pbrShadowLocs);


	//instanced pbr shader;
	instancedPbrShader = LoadShader(
		"Shaders/pbr_tangent_instanced.vs",
		"Shaders/pbr_tangent.fs"
	);

	instancedPbrShader.locs[SHADER_LOC_MATRIX_MVP] =
		GetShaderLocation(instancedPbrShader, "mvp");

	instancedPbrShader.locs[SHADER_LOC_VERTEX_TANGENT] =
		GetShaderLocation(instancedPbrShader, "vertexTangent");

	instancedPbrShader.locs[SHADER_LOC_MAP_ALBEDO] =
		GetShaderLocation(instancedPbrShader, "albedoMap");

	instancedPbrShader.locs[SHADER_LOC_MAP_METALNESS] =
		GetShaderLocation(instancedPbrShader, "mraMap");

	instancedPbrShader.locs[SHADER_LOC_MAP_NORMAL] =
		GetShaderLocation(instancedPbrShader, "normalMap");

	instancedPbrShader.locs[SHADER_LOC_MAP_EMISSION] =
		GetShaderLocation(instancedPbrShader, "emissiveMap");

	instancedPbrShader.locs[SHADER_LOC_MAP_ROUGHNESS] =
		GetShaderLocation(instancedPbrShader, "roughnessMap");

	instancedPbrShader.locs[SHADER_LOC_MAP_OCCLUSION] =
		GetShaderLocation(instancedPbrShader, "occlusionMap");

	instancedPbrShader.locs[SHADER_LOC_MAP_HEIGHT] =
		GetShaderLocation(instancedPbrShader, "shadowMap0");

	instancedPbrShader.locs[SHADER_LOC_MAP_CUBEMAP] =
		GetShaderLocation(instancedPbrShader, "environmentMap");

	instancedPbrShader.locs[SHADER_LOC_VECTOR_VIEW] =
		GetShaderLocation(instancedPbrShader, "viewPos");

	instancedPbrShader.locs[SHADER_LOC_COLOR_DIFFUSE] =
		GetShaderLocation(instancedPbrShader, "albedoColor");

	instancedTilingLoc = GetShaderLocation(instancedPbrShader, "tiling");
	instancedOffsetLoc = GetShaderLocation(instancedPbrShader, "offset");

	instancedMetallicValueLoc = GetShaderLocation(instancedPbrShader, "metallicValue");
	instancedRoughnessValueLoc = GetShaderLocation(instancedPbrShader, "roughnessValue");
	instancedAoValueLoc = GetShaderLocation(instancedPbrShader, "aoValue");
	instancedNormalValueLoc = GetShaderLocation(instancedPbrShader, "normalValue");
	instancedEmissivePowerLoc = GetShaderLocation(instancedPbrShader, "emissivePower");
	instancedReflectionStrengthLoc = GetShaderLocation(instancedPbrShader, "reflectionStrength");

	instancedAlbedoColorLoc = GetShaderLocation(instancedPbrShader, "albedoColor");
	instancedEmissiveColorLoc = GetShaderLocation(instancedPbrShader, "emissiveColor");

	instancedAlphaModeLoc = GetShaderLocation(instancedPbrShader, "alphaMode");
	instancedAlphaCutoffLoc = GetShaderLocation(instancedPbrShader, "alphaCutoff");
	instancedReceiveShadowsLoc = GetShaderLocation(instancedPbrShader, "receiveShadows");

	instancedUseTexAlbedoLoc = GetShaderLocation(instancedPbrShader, "useTexAlbedo");
	instancedUseTexNormalLoc = GetShaderLocation(instancedPbrShader, "useTexNormal");
	instancedUseTexMRALoc = GetShaderLocation(instancedPbrShader, "useTexMRA");
	instancedUseTexEmissiveLoc = GetShaderLocation(instancedPbrShader, "useTexEmissive");

	instancedUseTexMetallicLoc = GetShaderLocation(instancedPbrShader, "useTexMetallic");
	instancedUseTexRoughnessLoc = GetShaderLocation(instancedPbrShader, "useTexRoughness");
	instancedUseTexAOLoc = GetShaderLocation(instancedPbrShader, "useTexAO");
	instancedUseGltfMetallicRoughnessLoc = GetShaderLocation(instancedPbrShader, "useGltfMetallicRoughness");

	CacheLightUniformLocations(instancedPbrShader, instancedLightLocs);

	instancedReflectionStrengthLoc =
		GetShaderLocation(instancedPbrShader, "reflectionStrength");

	CacheShadowUniformLocations(instancedPbrShader, instancedShadowLocs);
	//outline

	outlineShader = LoadShader("Shaders/outline.vs", "Shaders/outline.fs");

	outlineShader.locs[SHADER_LOC_MATRIX_MODEL] =
		GetShaderLocation(outlineShader, "matModel");

	outlineShader.locs[SHADER_LOC_VERTEX_TANGENT] =
		GetShaderLocation(outlineShader, "vertexTangent");

	outlineWidthLoc = GetShaderLocation(outlineShader, "outlineWidth");
	outlineColorLoc = GetShaderLocation(outlineShader, "outlineColor");

	outlineViewLoc = GetShaderLocation(outlineShader, "matView");
	outlineProjectionLoc = GetShaderLocation(outlineShader, "matProjection");

	//shadow

	shadowDepthShader = LoadShader(
		"Shaders/shadow_depth.vs",
		"Shaders/shadow_depth.fs"
	);

	TraceLog(LOG_INFO, "Shadow depth shader id = %u", shadowDepthShader.id);

	shadowDepthShader.locs[SHADER_LOC_MATRIX_MVP] =
		GetShaderLocation(shadowDepthShader, "mvp");

	shadowDepthAnimatedShader = LoadShader(
		"Shaders/shadow_depth_animated.vs",
		"Shaders/shadow_depth.fs"
	);

	shadowDepthAnimatedShader.locs[SHADER_LOC_MATRIX_MVP] =
		GetShaderLocation(shadowDepthAnimatedShader, "mvp");

	shadowDepthAnimatedShader.locs[SHADER_LOC_VERTEX_BONEIDS] =
		GetShaderLocationAttrib(shadowDepthAnimatedShader, "vertexBoneIndices");

	shadowDepthAnimatedShader.locs[SHADER_LOC_VERTEX_BONEWEIGHTS] =
		GetShaderLocationAttrib(shadowDepthAnimatedShader, "vertexBoneWeights");

	for (int i = 0; i < MAX_SHADOW_CASTERS; i++)
	{
		shadowCasters[i].shadowMap =
			LoadShadowMapRenderTexture(shadowMapSize, shadowMapSize);

		SetTextureFilter(shadowCasters[i].shadowMap.depth, TEXTURE_FILTER_POINT);
		SetTextureWrap(shadowCasters[i].shadowMap.depth, TEXTURE_WRAP_CLAMP);

		SetTextureFilter(shadowCasters[i].shadowMap.texture, TEXTURE_FILTER_POINT);
		SetTextureWrap(shadowCasters[i].shadowMap.texture, TEXTURE_WRAP_CLAMP);
	}
	MarkShadowTextureBindingsDirty(true);

	albedoColorLoc = pbrShader.locs[SHADER_LOC_COLOR_DIFFUSE];
	aoValueLoc = GetShaderLocation(pbrShader, "aoValue");
	normalValueLoc = GetShaderLocation(pbrShader, "normalValue");
	offsetLoc = GetShaderLocation(pbrShader, "offset");
	metallicValueLoc = GetShaderLocation(pbrShader, "metallicValue");
	roughnessValueLoc = GetShaderLocation(pbrShader, "roughnessValue");
	emissiveIntensityLoc = GetShaderLocation(pbrShader, "emissivePower");
	emissiveColorLoc = GetShaderLocation(pbrShader, "emissiveColor");
	textureTilingLoc = GetShaderLocation(pbrShader, "tiling");
	lightCountLoc = GetShaderLocation(pbrShader, "numOfLights");

	pbrUseTexAlbedoLoc = GetShaderLocation(pbrShader, "useTexAlbedo");
	pbrUseTexNormalLoc = GetShaderLocation(pbrShader, "useTexNormal");
	pbrUseTexMRALoc = GetShaderLocation(pbrShader, "useTexMRA");
	pbrUseTexEmissiveLoc = GetShaderLocation(pbrShader, "useTexEmissive");

	pbrUseTexMetallicLoc = GetShaderLocation(pbrShader, "useTexMetallic");
	pbrUseTexRoughnessLoc = GetShaderLocation(pbrShader, "useTexRoughness");
	pbrUseTexAOLoc = GetShaderLocation(pbrShader, "useTexAO");
	pbrUseGltfMetallicRoughnessLoc = GetShaderLocation(pbrShader, "useGltfMetallicRoughness");

	pbrAlphaModeLoc = GetShaderLocation(pbrShader, "alphaMode");
	pbrAlphaCutoffLoc = GetShaderLocation(pbrShader, "alphaCutoff");
	pbrReceiveShadowsLoc = GetShaderLocation(pbrShader, "receiveShadows");

	CacheLightUniformLocations(pbrShader, pbrLightLocs);

	//blur 
	blurScaleLoc = GetShaderLocation(blurShader, "blurScale");

	//animation Non-normals Shaders
	animatedPbrShader = LoadShader(
		"Shaders/pbr_animated.vs",
		"Shaders/pbr_animated.fs"
	);

	Image neutralNormalImage = GenImageColor(1, 1, Color{ 128, 128, 255, 255 });
	neutralNormalTexture = LoadTextureFromImage(neutralNormalImage);
	UnloadImage(neutralNormalImage);

	// glTF metallicRoughness fallback:
	// R unused, G roughness, B metallic.
	// roughness = about 0.65, metallic = 0.
	Image defaultMRImage = GenImageColor(1, 1, Color{ 0, 166, 0, 255 });
	defaultGltfMRTexture = LoadTextureFromImage(defaultMRImage);
	UnloadImage(defaultMRImage);

	animatedPbrShader.locs[SHADER_LOC_MAP_HEIGHT] =
		GetShaderLocation(animatedPbrShader, "shadowMap0");


	animatedBoneMatricesLoc =
		GetShaderLocation(animatedPbrShader, "boneMatrices");
	animatedPbrShader.locs[SHADER_LOC_VERTEX_BONEIDS] =
		GetShaderLocationAttrib(animatedPbrShader, "vertexBoneIndices");

	animatedPbrShader.locs[SHADER_LOC_VERTEX_BONEWEIGHTS] =
		GetShaderLocationAttrib(animatedPbrShader, "vertexBoneWeights");

	animatedPbrShader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(animatedPbrShader, "mvp");
	animatedPbrShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(animatedPbrShader, "matModel");
	animatedPbrShader.locs[SHADER_LOC_MAP_ALBEDO] = GetShaderLocation(animatedPbrShader, "albedoMap");
	animatedPbrShader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(animatedPbrShader, "viewPos");
	animatedPbrShader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(animatedPbrShader, "albedoColor");

	animatedPbrShader.locs[SHADER_LOC_VERTEX_TANGENT] =
		GetShaderLocationAttrib(animatedPbrShader, "vertexTangent");

	animatedPbrShader.locs[SHADER_LOC_MAP_NORMAL] =
		GetShaderLocation(animatedPbrShader, "normalMap");

	animatedPbrShader.locs[SHADER_LOC_MAP_METALNESS] =
		GetShaderLocation(animatedPbrShader, "mraMap");

	animatedPbrShader.locs[SHADER_LOC_MAP_ROUGHNESS] =
		GetShaderLocation(animatedPbrShader, "roughnessMap");

	animatedPbrShader.locs[SHADER_LOC_MAP_OCCLUSION] =
		GetShaderLocation(animatedPbrShader, "occlusionMap");

	animatedPbrShader.locs[SHADER_LOC_MAP_EMISSION] =
		GetShaderLocation(animatedPbrShader, "emissiveMap");

	CacheLightUniformLocations(animatedPbrShader, animatedLightLocs);
	CacheShadowUniformLocations(animatedPbrShader, animatedShadowLocs);

	int maxLightCount = MAX_LIGHTS;
	SetShaderValue(pbrShader, lightCountLoc, &maxLightCount, SHADER_UNIFORM_INT);

	floorAlbedo = LoadTexture("Textures/marble_01_diff_1k.png");
	floorNormal = LoadTexture("Textures/marble_01_nor_gl_1k.png");
	floorMRA = LoadTexture("Textures/marble_MRA.png");

	// Default shader usage like the example
	SetTextureUsage(pbrShader, 1, 1, 1, 0);

	//skybox
	LoadSkybox("resources/DayBox.png", false);
	reflectionStrength = 3.0f;
	SetShaderValue(pbrShader, reflectionStrengthLoc, &reflectionStrength, SHADER_UNIFORM_FLOAT);
}

void Game::ResetPlayerToSpawn()
{
	player.m_pos = playerSpawnPosition;
	player.m_velocity = { 0.0f, 0.0f, 0.0f };
	player.m_dir = { 0.0f, 0.0f, 0.0f };

	player.m_forward = 0;
	player.m_sideway = 0;
	player.m_jumpPressed = false;

	player.yaw = playerSpawnYaw;
	player.pitch = 0.0f;

	headTimer = 0.0f;
	walkLerp = 0.0f;
	lean = { 0.0f, 0.0f };

	// Important: also move the Jolt virtual character.
	// Otherwise the next SyncPlayerFromVirtual() can pull the player
	// back to the old pre-reset position.
	if (playerCharacter)
	{
		playerCharacter->SetPosition(
			JPH::RVec3(
				playerSpawnPosition.x,
				playerSpawnPosition.y + playerHeight * 0.5f,
				playerSpawnPosition.z
			)
		);

		playerCharacter->SetLinearVelocity(JPH::Vec3::sZero());
	}

	camera.position = {
		player.m_pos.x,
		player.m_pos.y + (BOTTOM_HEIGHT + headLerp),
		player.m_pos.z
	};

	Vector3 forward = {
		sinf(player.yaw),
		0.0f,
		-cosf(player.yaw)
	};

	camera.target = Vector3Add(camera.position, forward);
	camera.fovy = 60.0f;
}

void Game::setLighting()
{
	SetShaderValue(
		pbrShader,
		GetShaderLocation(pbrShader, "ambientColor"),
		&worldAmbientColorCached,
		SHADER_UNIFORM_VEC3
	);

	SetShaderValue(
		pbrShader,
		GetShaderLocation(pbrShader, "ambient"),
		&worldAmbientIntensityCached,
		SHADER_UNIFORM_FLOAT
	);

	lightCount = 0;
	lights[0] = CreateLight(
		LIGHT_POINT,
		{ -1.0f, 1.0f, -2.0f },
		{ 0.0f, 0.0f, 0.0f },
		YELLOW,
		4.0f
	);

	lights[0].castsShadow = 1;
	lights[0].target = { 0.0f, 0.9f, 0.0f };

	lights[0].range = 6.0f;
	lights[0].shadowRange = 6.0f;

	lights[0].shadowOrthoSize = 9.0f;
	lights[0].shadowNear = 0.1f;
	lights[0].shadowFar = 24.0f;
	lights[0].shadowBias = 0.005f;
	lights[0].shadowStrength = 0.5f;

	lights[0].name = "Yellow Light";

	lights[1] = CreateLight(LIGHT_POINT, { 2.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, GREEN, 6.0f);
	lights[1].name = "Green Light";
	lights[1].range = 5.5f;

	lights[2] = CreateLight(LIGHT_POINT, { -2.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, RED, 8.3f);
	lights[2].name = "Red Light";
	lights[2].range = 5.5f;

	lights[3] = CreateLight(LIGHT_POINT, { 1.0f, 1.0f, -2.0f }, { 0.0f, 0.0f, 0.0f }, BLUE, 2.0f);
	lights[3].name = "Blue Light";
	lights[3].range = 5.5f;

	lights[4] = CreateLight(LIGHT_POINT, { 2.0f, 1.0f, -2.0f }, { 0.0f, 0.0f, 0.0f }, BLUE, 2.0f);
	lights[4].name = "White Light";
	lights[4].range = 5.5f;
}

void Game::UploadWorldLightingToShader() const
{
	SetShaderValueIfValid(
		pbrShader,
		lightCountLoc,
		&lightCount,
		SHADER_UNIFORM_INT
	);

	float cameraPos[3] = {
		camera.position.x,
		camera.position.y,
		camera.position.z
	};

	SetShaderValueIfValid(
		pbrShader,
		pbrShader.locs[SHADER_LOC_VECTOR_VIEW],
		cameraPos,
		SHADER_UNIFORM_VEC3
	);

	SetShaderValueIfValid(
		pbrShader,
		pbrAmbientColorLoc,
		&worldAmbientColorCached,
		SHADER_UNIFORM_VEC3
	);

	SetShaderValueIfValid(
		pbrShader,
		pbrAmbientLoc,
		&worldAmbientIntensityCached,
		SHADER_UNIFORM_FLOAT
	);

	for (int i = 0; i < lightCount; i++)
	{
		UpdateLight(lights[i]);
	}
}

void Game::CacheLightUniformLocations(
	Shader shader,
	LightUniformLocations& locs
)
{
	locs.numOfLights = GetShaderLocation(shader, "numOfLights");
	locs.viewPos = GetShaderLocation(shader, "viewPos");
	locs.ambientColor = GetShaderLocation(shader, "ambientColor");
	locs.ambient = GetShaderLocation(shader, "ambient");

	for (int i = 0; i < MAX_LIGHTS; i++)
	{
		locs.enabled[i] = GetShaderLocation(shader, TextFormat("lights[%i].enabled", i));
		locs.type[i] = GetShaderLocation(shader, TextFormat("lights[%i].type", i));
		locs.position[i] = GetShaderLocation(shader, TextFormat("lights[%i].position", i));
		locs.target[i] = GetShaderLocation(shader, TextFormat("lights[%i].target", i));
		locs.color[i] = GetShaderLocation(shader, TextFormat("lights[%i].color", i));
		locs.intensity[i] = GetShaderLocation(shader, TextFormat("lights[%i].intensity", i));
		locs.range[i] = GetShaderLocation(shader, TextFormat("lights[%i].range", i));
	}
}

void Game::UploadInspectionLightingToShader(const Camera3D& cam) const
{
	Vector3 forward = NormalizeSafe(Vector3Subtract(cam.target, cam.position));
	Vector3 right = NormalizeSafe(Vector3CrossProduct(forward, { 0.0f, 1.0f, 0.0f }));
	if (Vector3Length(right) < 0.0001f) right = { 1.0f, 0.0f, 0.0f };
	Vector3 up = NormalizeSafe(Vector3CrossProduct(right, forward));

	Vector3 lightPos = cam.position;
	lightPos = Vector3Add(lightPos, Vector3Scale(forward, inspectLightForwardOffset));
	lightPos = Vector3Add(lightPos, Vector3Scale(right, inspectLightRightOffset));
	lightPos = Vector3Add(lightPos, Vector3Scale(up, inspectLightUpOffset));

	int inspectLightCount = 1;
	SetShaderValue(pbrShader, lightCountLoc, &inspectLightCount, SHADER_UNIFORM_INT);

	float cameraPos[3] = { cam.position.x, cam.position.y, cam.position.z };
	SetShaderValue(pbrShader, pbrShader.locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);

	Vector3 ambientColor = inspectAmbientColor;
	float ambientIntensity = inspectAmbientIntensity;

	if (musicBoxInspectActive)
	{
		// Much darker ambient during the music box easter egg.
		ambientColor = {
			0.025f,
			0.025f,
			0.035f
		};

		ambientIntensity *= musicBoxInspectAmbientMultiplier;
	}

	SetShaderValue(
		pbrShader,
		GetShaderLocation(pbrShader, "ambientColor"),
		&ambientColor,
		SHADER_UNIFORM_VEC3
	);

	SetShaderValue(
		pbrShader,
		GetShaderLocation(pbrShader, "ambient"),
		&ambientIntensity,
		SHADER_UNIFORM_FLOAT
	);

	int enabled = 1;
	int type = LIGHT_POINT;
	float target[3] = { 0.0f, 0.0f, 0.0f };
	float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float intensity = inspectLightIntensity;

	if (musicBoxInspectActive)
	{
		// Slightly cold/dirty white light for a creepier music box effect.
		color[0] = 0.82f;
		color[1] = 0.88f;
		color[2] = 1.0f;
		color[3] = 1.0f;

		// Main flicker multiplier.
		intensity *= musicBoxInspectLightMultiplier;

		// Make the low points more dramatic.
		if (musicBoxInspectLightMultiplier < 0.18f)
		{
			intensity *= 0.25f;
		}
	}
	float position[3] = { lightPos.x, lightPos.y, lightPos.z };

	SetShaderValue(pbrShader, GetShaderLocation(pbrShader, "lights[0].enabled"), &enabled, SHADER_UNIFORM_INT);
	SetShaderValue(pbrShader, GetShaderLocation(pbrShader, "lights[0].type"), &type, SHADER_UNIFORM_INT);
	SetShaderValue(pbrShader, GetShaderLocation(pbrShader, "lights[0].position"), position, SHADER_UNIFORM_VEC3);
	SetShaderValue(pbrShader, GetShaderLocation(pbrShader, "lights[0].target"), target, SHADER_UNIFORM_VEC3);
	SetShaderValue(pbrShader, GetShaderLocation(pbrShader, "lights[0].color"), color, SHADER_UNIFORM_VEC4);
	SetShaderValue(pbrShader, GetShaderLocation(pbrShader, "lights[0].intensity"), &intensity, SHADER_UNIFORM_FLOAT);

	int disabled = 0;
	for (int i = 1; i < MAX_LIGHTS; i++)
	{
		SetShaderValue(
			pbrShader,
			GetShaderLocation(pbrShader, TextFormat("lights[%i].enabled", i)),
			&disabled,
			SHADER_UNIFORM_INT
		);
	}
}
void Game::EnterInspectMode()
{
	if (!hasHeldBody) return;

	inspectMode = true;
	inspectTransitionFrames = 3;
	inspectJustEntered = true;

	inspectYawDeg = 0.0f;
	inspectPitchDeg = 0.0f;

	inspectSmoothedPos = physics->GetBodyPosition(heldBody);
	inspectSmoothedRot = physics->GetBodyRotation(heldBody);
	inspectSmoothingInitialized = true;

	inspectRestoreCursorLocked = !cursorUnlocked;

	StartSelfInspectDialogueForHeldItem();
	BeginMusicBoxInspectEasterEgg();
}

void Game::ExitInspectMode()
{
	StopLoopingSfx();
	EndMusicBoxInspectEasterEgg();

	if (IsSelfInspectDialogueActive())
	{
		CloseDialogue();
	}

	inspectMode = false;
	inspectYawDeg = 0.0f;
	inspectPitchDeg = 0.0f;
	inspectSmoothingInitialized = false;
}

void Game::UpdateInspectMode(float dt)
{

	if (!inspectMode || !hasHeldBody) return;

	if (IsKeyPressed(KEY_ESCAPE))
	{
		ExitInspectMode();
		return;
	}

	if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
	{
		Vector2 md = GetMouseDelta();

		inspectYawDeg += md.x * inspectRotateSpeed;
		inspectPitchDeg += md.y * inspectRotateSpeed;
		inspectPitchDeg = Clamp(inspectPitchDeg, -179.0f, 179.0f);
	}

	float wheel = GetMouseWheelMove();
	if (wheel != 0.0f)
	{
		inspectDistance -= wheel * inspectZoomStep;
		inspectDistance = Clamp(inspectDistance, inspectMinDistance, inspectMaxDistance);
	}

	UpdateLoopingSfx();

}
void Game::createTestScene()
{
	//Mesh floorMesh = GenMeshPlane(20.0f, 20.0f, 8, 8);
	//GenMeshTangents(&floorMesh);
	//pbrFloor = LoadModelFromMesh(floorMesh);

	Mesh blockoutMesh = GenMeshCube(1.0f, 1.0f, 1.0f);
	blockoutCubeModel = LoadModelFromMesh(blockoutMesh);
	blockoutCubeModel.materials[0].shader = pbrShader;
	blockoutCubeModel.materials[0].maps[MATERIAL_MAP_ALBEDO].color = WHITE;
	blockoutCubeModel.materials[0].maps[MATERIAL_MAP_METALNESS].value = 0.0f;
	blockoutCubeModel.materials[0].maps[MATERIAL_MAP_ROUGHNESS].value = 0.8f;
	blockoutCubeModel.materials[0].maps[MATERIAL_MAP_OCCLUSION].value = 1.0f;
	blockoutCubeModel.materials[0].maps[MATERIAL_MAP_EMISSION].color = BLACK;



	for (int i = 0; i < pbrFloor.meshCount; i++)
	{
		if (!pbrFloor.meshes[i].tangents)
		{
			GenMeshTangents(&pbrFloor.meshes[i]);
			UploadMesh(&pbrFloor.meshes[i], false);
		}
	}

	for (int i = 0; i < pbrFloor.materialCount; i++)
	{
		pbrFloor.materials[i].shader = pbrShader;

		pbrFloor.materials[i].maps[MATERIAL_MAP_ALBEDO].texture = floorAlbedo;
		pbrFloor.materials[i].maps[MATERIAL_MAP_NORMAL].texture = floorNormal;
		pbrFloor.materials[i].maps[MATERIAL_MAP_METALNESS].texture = floorMRA;

		pbrFloor.materials[i].maps[MATERIAL_MAP_ALBEDO].color = WHITE;
		pbrFloor.materials[i].maps[MATERIAL_MAP_METALNESS].value = 0.8f;
		pbrFloor.materials[i].maps[MATERIAL_MAP_ROUGHNESS].value = 0.1f;
		pbrFloor.materials[i].maps[MATERIAL_MAP_OCCLUSION].value = 1.0f;
		pbrFloor.materials[i].maps[MATERIAL_MAP_EMISSION].color = BLACK;
	}

	/*
	Mesh cubeMesh = GenMeshCube(1.0f, 1.0f, 1.0f);
	GenMeshTangents(&cubeMesh);

	testProp = LoadModelFromMesh(cubeMesh);
	testProp.materials[0].shader = pbrShader;
	testProp.materials[0].maps[MATERIAL_MAP_ALBEDO].color = { 190, 90, 70, 255 };
	testProp.materials[0].maps[MATERIAL_MAP_METALNESS].value = 0.0f;
	testProp.materials[0].maps[MATERIAL_MAP_ROUGHNESS].value = 0.35f;
	testProp.materials[0].maps[MATERIAL_MAP_OCCLUSION].value = 1.0f;
	testProp.materials[0].maps[MATERIAL_MAP_EMISSION].color = BLACK;

	testProp.materials[0].shader = pbrShader;
	*/


	for (int i = 0; i < Shelf.meshCount; i++)
	{
		if (!Shelf.meshes[i].tangents)
		{
			GenMeshTangents(&Shelf.meshes[i]);
			UploadMesh(&Shelf.meshes[i], false);
		}
	}

	for (int i = 0; i < Shelf.materialCount; i++)
	{
		Shelf.materials[i].shader = pbrShader;

		Shelf.materials[i].maps[MATERIAL_MAP_ALBEDO].texture = shelfAlbedo;
		Shelf.materials[i].maps[MATERIAL_MAP_NORMAL].texture = shelfNormal;
		Shelf.materials[i].maps[MATERIAL_MAP_METALNESS].texture = shelfMRA;

		Shelf.materials[i].maps[MATERIAL_MAP_ALBEDO].color = WHITE;
		Shelf.materials[i].maps[MATERIAL_MAP_METALNESS].value = 0.0f;
		Shelf.materials[i].maps[MATERIAL_MAP_ROUGHNESS].value = 0.5f;
		Shelf.materials[i].maps[MATERIAL_MAP_OCCLUSION].value = 0.2f;
		Shelf.materials[i].maps[MATERIAL_MAP_EMISSION].color = BLACK;
	}

	for (int i = 0; i < gBoy.meshCount; i++)
	{
		if (!gBoy.meshes[i].tangents)
		{
			GenMeshTangents(&gBoy.meshes[i]);
			UploadMesh(&gBoy.meshes[i], false);
		}
	}

	for (int i = 0; i < gBoy.materialCount; i++)
	{
		gBoy.materials[i].shader = pbrShader;

		gBoy.materials[i].maps[MATERIAL_MAP_ALBEDO].texture = gBoyAlbedo;
		gBoy.materials[i].maps[MATERIAL_MAP_NORMAL].texture = gBoyNormal;
		gBoy.materials[i].maps[MATERIAL_MAP_METALNESS].texture = gBoyMRA;

		gBoy.materials[i].maps[MATERIAL_MAP_ALBEDO].color = WHITE;
		gBoy.materials[i].maps[MATERIAL_MAP_METALNESS].value = 0.0f;
		gBoy.materials[i].maps[MATERIAL_MAP_ROUGHNESS].value = 0.0f;
		gBoy.materials[i].maps[MATERIAL_MAP_OCCLUSION].value = 0.0f;
		//gBoy.materials[i].maps[MATERIAL_MAP_EMISSION].color = BLACK;
	}



	for (int i = 0; i < Gacha.materialCount; i++)
	{
		Gacha.materials[i].shader = pbrShader;

		Gacha.materials[i].maps[MATERIAL_MAP_ALBEDO].color = WHITE;
		Gacha.materials[i].maps[MATERIAL_MAP_METALNESS].value = 0.0f;
		Gacha.materials[i].maps[MATERIAL_MAP_ROUGHNESS].value = 0.7f;
		Gacha.materials[i].maps[MATERIAL_MAP_OCCLUSION].value = 1.0f;
		Gacha.materials[i].maps[MATERIAL_MAP_EMISSION].color = BLACK;

	}



	for (int i = 0; i < customerModel.materialCount; i++)
	{
		customerModel.materials[i].shader = animatedPbrShader;

		customerModel.materials[i].maps[MATERIAL_MAP_ALBEDO].color = WHITE;
		customerModel.materials[i].maps[MATERIAL_MAP_METALNESS].value = 0.0f;
		customerModel.materials[i].maps[MATERIAL_MAP_ROUGHNESS].value = 0.65f;
		customerModel.materials[i].maps[MATERIAL_MAP_OCCLUSION].value = 1.0f;
		customerModel.materials[i].maps[MATERIAL_MAP_EMISSION].color = BLACK;
	}

	// Apply reflection cubemap to all PBR models
	//ApplyEnvironmentCubemap(blockoutCubeModel);
	//ApplyEnvironmentCubemap(pbrFloor);
	//ApplyEnvironmentCubemap(testProp);
	//ApplyEnvironmentCubemap(Shelf);
	//ApplyEnvironmentCubemap(gBoy);
	ApplyEnvironmentCubemap(Gacha);

	// Special tweak for the gacha glass shell
	// Assuming material index 4 is the transparent shell
	Gacha.materials[4].maps[MATERIAL_MAP_ROUGHNESS].value = 0.08f;
	Gacha.materials[4].maps[MATERIAL_MAP_METALNESS].value = 0.0f;

	BuildOutlineModelCache(&gBoy);
	BuildOutlineModelCache(&Shelf);
	BuildOutlineModelCache(&Gacha);

}

void Game::PrepareCustomerPBRMaterial() const
{
	Vector2 oneTiling = { 1.0f, 1.0f };
	Vector2 zeroOffset = { 0.0f, 0.0f };

	float metallic = 0.0f;
	float roughness = 0.65f;
	float ao = 1.0f;
	float normalStrength = 1.0f;
	float emissiveIntensity = 1.0f;

	Vector4 albedoColor = ColorNormalize(WHITE);
	Vector4 emissiveColor = ColorNormalize(BLACK);

	SetShaderValue(pbrShader, textureTilingLoc, &oneTiling, SHADER_UNIFORM_VEC2);
	SetShaderValue(pbrShader, offsetLoc, &zeroOffset, SHADER_UNIFORM_VEC2);

	SetShaderValue(pbrShader, metallicValueLoc, &metallic, SHADER_UNIFORM_FLOAT);
	SetShaderValue(pbrShader, roughnessValueLoc, &roughness, SHADER_UNIFORM_FLOAT);
	SetShaderValue(pbrShader, aoValueLoc, &ao, SHADER_UNIFORM_FLOAT);
	SetShaderValue(pbrShader, normalValueLoc, &normalStrength, SHADER_UNIFORM_FLOAT);
	SetShaderValue(pbrShader, emissiveIntensityLoc, &emissiveIntensity, SHADER_UNIFORM_FLOAT);

	SetShaderValue(pbrShader, albedoColorLoc, &albedoColor, SHADER_UNIFORM_VEC4);
	SetShaderValue(pbrShader, emissiveColorLoc, &emissiveColor, SHADER_UNIFORM_VEC4);

	// Customer test: albedo only, no normal map, no MRA.
	SetTextureUsage(pbrShader, 1, 0, 0, 0);
}

void Game::ApplyAnimatedMaterialUniforms(const Material& mat) const
{
	int useTexAlbedo =
		mat.maps[MATERIAL_MAP_ALBEDO].texture.id != 0 ? 1 : 0;

	int useTexNormal =
		mat.maps[MATERIAL_MAP_NORMAL].texture.id != 0 ? 1 : 0;

	int useTexMRA =
		mat.maps[MATERIAL_MAP_METALNESS].texture.id != 0 ? 1 : 0;

	int useTexRoughness =
		mat.maps[MATERIAL_MAP_ROUGHNESS].texture.id != 0 ? 1 : 0;

	int useTexAO =
		mat.maps[MATERIAL_MAP_OCCLUSION].texture.id != 0 ? 1 : 0;

	int useTexEmissive =
		mat.maps[MATERIAL_MAP_EMISSION].texture.id != 0 ? 1 : 0;

	float normalStrength = useTexNormal ? 0.35f : 0.0f;

	Vector4 albedoColor = ColorNormalize(
		mat.maps[MATERIAL_MAP_ALBEDO].color
	);

	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexAlbedo"), &useTexAlbedo, SHADER_UNIFORM_INT);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexNormal"), &useTexNormal, SHADER_UNIFORM_INT);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexMRA"), &useTexMRA, SHADER_UNIFORM_INT);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexRoughness"), &useTexRoughness, SHADER_UNIFORM_INT);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexAO"), &useTexAO, SHADER_UNIFORM_INT);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexEmissive"), &useTexEmissive, SHADER_UNIFORM_INT);

	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "normalStrength"), &normalStrength, SHADER_UNIFORM_FLOAT);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "albedoColor"), &albedoColor, SHADER_UNIFORM_VEC4);
}
void Game::DrawCustomerPBR(const Customer& customer) const
{
	if (customer.model == nullptr)
		return;

	Model& model = *customer.model;

	Matrix transform = MatrixIdentity();

	transform = MatrixMultiply(
		transform,
		MatrixScale(customer.scale.x, customer.scale.y, customer.scale.z)
	);

	transform = MatrixMultiply(
		transform,
		MatrixRotate({ 1.0f, 0.0f, 0.0f }, 90.0f * DEG2RAD)
	);

	transform = MatrixMultiply(
		transform,
		MatrixTranslate(
			customer.position.x,
			customer.position.y,
			customer.position.z
		)
	);

	rlDisableBackfaceCulling();

	for (int meshIndex = 0; meshIndex < model.meshCount; meshIndex++)
	{
		int matIndex = 0;

		if (model.meshMaterial != nullptr)
		{
			matIndex = model.meshMaterial[meshIndex];
		}

		if (matIndex < 0 || matIndex >= model.materialCount)
		{
			matIndex = 0;
		}

		Material mat = model.materials[matIndex];
		mat.shader = animatedPbrShader;

		ApplyAnimatedMaterialUniforms(mat);

		DrawMesh(
			model.meshes[meshIndex],
			mat,
			transform
		);
	}

	rlEnableBackfaceCulling();
}
void Game::PrepareAnimatedCustomerPBRMaterial() const
{
	Vector2 oneTiling = { 1.0f, 1.0f };
	Vector2 zeroOffset = { 0.0f, 0.0f };

	float metallic = 0.0f;
	float roughness = 0.65f;
	float ao = 1.0f;
	float emissiveIntensity = 1.0f;
	float reflectionStrengthValue = 0.0f;

	// Keep normal strength low for now.
	float normalStrength = 0.35f;

	Vector4 albedoColor = ColorNormalize(WHITE);
	Vector4 emissiveColor = ColorNormalize(WHITE);

	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "tiling"), &oneTiling, SHADER_UNIFORM_VEC2);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "offset"), &zeroOffset, SHADER_UNIFORM_VEC2);

	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "metallicValue"), &metallic, SHADER_UNIFORM_FLOAT);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "roughnessValue"), &roughness, SHADER_UNIFORM_FLOAT);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "aoValue"), &ao, SHADER_UNIFORM_FLOAT);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "emissivePower"), &emissiveIntensity, SHADER_UNIFORM_FLOAT);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "reflectionStrength"), &reflectionStrengthValue, SHADER_UNIFORM_FLOAT);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "normalStrength"), &normalStrength, SHADER_UNIFORM_FLOAT);

	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "albedoColor"), &albedoColor, SHADER_UNIFORM_VEC4);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "emissiveColor"), &emissiveColor, SHADER_UNIFORM_VEC4);

	int useTexAlbedo = 1;

	// TEMPORARY SAFE SETTINGS
	// Turn normal maps off until per-material drawing is working.
	int useTexNormal = 0;

	int useTexMRA = 0;
	int useTexMetallic = 0;
	int useTexRoughness = 0;
	int useTexAO = 0;
	int useTexEmissive = 0;

	// For GLB/glTF metallicRoughness textures:
	// G = roughness, B = metallic.
	int useGltfMetallicRoughness = 1;

	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexAlbedo"), &useTexAlbedo, SHADER_UNIFORM_INT);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexNormal"), &useTexNormal, SHADER_UNIFORM_INT);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexMRA"), &useTexMRA, SHADER_UNIFORM_INT);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexMetallic"), &useTexMetallic, SHADER_UNIFORM_INT);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexRoughness"), &useTexRoughness, SHADER_UNIFORM_INT);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexAO"), &useTexAO, SHADER_UNIFORM_INT);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useTexEmissive"), &useTexEmissive, SHADER_UNIFORM_INT);
	SetShaderValue(animatedPbrShader, GetShaderLocation(animatedPbrShader, "useGltfMetallicRoughness"), &useGltfMetallicRoughness, SHADER_UNIFORM_INT);
}
void Game::updatePBRShader()
{
	if (pbrShader.id == 0)
		return;

	if (ShouldMusicBoxBlackoutScene())
	{
		UploadBlackoutLightingToShader(
			pbrShader,
			pbrLightLocs,
			camera.position
		);

		UploadBlackoutLightingToShader(
			animatedPbrShader,
			animatedLightLocs,
			camera.position
		);

		UploadBlackoutLightingToShader(
			instancedPbrShader,
			instancedLightLocs,
			camera.position
		);

		return;
	}

	UploadWorldLightingToShader();
}



Light Game::CreateLight(int type, Vector3 position, Vector3 target, Color color, float intensity)
{
	Light light{};

	if (lightCount < MAX_LIGHTS)
	{
		light.enabled = 1;
		light.type = type;
		light.position = position;
		light.target = target;
		light.color[0] = (float)color.r / 255.0f;
		light.color[1] = (float)color.g / 255.0f;
		light.color[2] = (float)color.b / 255.0f;
		light.color[3] = (float)color.a / 255.0f;
		light.intensity = intensity;

		light.enabledLoc = GetShaderLocation(pbrShader, TextFormat("lights[%i].enabled", lightCount));
		light.typeLoc = GetShaderLocation(pbrShader, TextFormat("lights[%i].type", lightCount));
		light.positionLoc = GetShaderLocation(pbrShader, TextFormat("lights[%i].position", lightCount));
		light.targetLoc = GetShaderLocation(pbrShader, TextFormat("lights[%i].target", lightCount));
		light.colorLoc = GetShaderLocation(pbrShader, TextFormat("lights[%i].color", lightCount));
		light.intensityLoc = GetShaderLocation(pbrShader, TextFormat("lights[%i].intensity", lightCount));
		light.rangeLoc = GetShaderLocation(
			pbrShader,
			TextFormat("lights[%i].range", lightCount)
		);
		UpdateLight(light);   // same idea as the basic example
		lightCount++;
	}

	return light;
}

float Game::GetFlatDistanceXZ(Vector3 a, Vector3 b) const
{
	float dx = b.x - a.x;
	float dz = b.z - a.z;

	return sqrtf(dx * dx + dz * dz);
}

void Game::UpdateCustomerAI(int customerIndex, float dt)
{

	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	Customer& customer = customers[customerIndex];
	;

	if (customer.pendingDespawn)
		return;

	if (customer.editorFrozen)
		return;

	int counter = FindPOIByKind(CustomerPOIKind::Counter);

	// Face the counter while waiting in queue.
	if ((customer.aiState == CustomerAIState::BrowserQueueing ||
		customer.aiState == CustomerAIState::SellerQueueing) &&
		counter >= 0 &&
		!customer.hasMoveTarget)
	{
		customer.FaceTowards(customerPOIs[counter].position, dt, 360.0f);
	}

	// Let current movement finish.
	if (customer.hasMoveTarget)
		return;

	if (customer.movementPauseTimer > 0.0f)
	{
		customer.movementPauseTimer -= dt;

		if (customer.movementPauseTimer < 0.0f)
			customer.movementPauseTimer = 0.0f;


		return;
	}

	if (customer.poiWaitTimer > 0.0f)
	{
		customer.poiWaitTimer -= dt;
		return;
	}

	switch (customer.aiState)
	{
	case CustomerAIState::BrowserBrowsing:
	{
		int itemPOI = FindRandomPOIByKindForCustomer(
			CustomerPOIKind::BrowseItem,
			customer
		);

		// Fallback for testing scenes where shelves are still Generic.
		if (itemPOI < 0)
		{
			itemPOI = PickNextCustomerDestinationPOI(customer);
		}

		if (itemPOI < 0)
		{
			customer.poiWaitTimer = 1.0f;
			return;
		}

		customer.assignedItemPOIIndex = itemPOI;
		customer.aiState = CustomerAIState::BrowserGoingToItem;

		StartCustomerRouteToPOI(customerIndex, itemPOI);
		return;
	}

	case CustomerAIState::BrowserGoingToItem:
	{
		customer.aiState = CustomerAIState::BrowserInspectingItem;

		if (customer.assignedItemPOIIndex >= 0 &&
			customer.assignedItemPOIIndex < (int)customerPOIs.size())
		{
			customer.poiWaitTimer = GetRandomPOIWaitSeconds(
				customerPOIs[customer.assignedItemPOIIndex]
			);
		}
		else
		{
			customer.poiWaitTimer = 2.0f;
		}

		break;
	}

	case CustomerAIState::BrowserInspectingItem:
	{
		bool finalBrowseVisit = customer.browseVisitsRemaining <= 1;

		if (finalBrowseVisit && !customer.hasPickedItem)
		{
			AssignTradeItemToCustomer(customerIndex, false);

			customer.hasPickedItem = true;

			// Buyer picks up item from the final browse POI.
			customer.PlayOneShotAnimation(CustomerAnimState::Give);

			// Let the Give animation show briefly before queueing.
			customer.poiWaitTimer = 0.75f;

			return;
		}

		customer.browseVisitsRemaining--;

		// Still browsing: force another POI immediately after max wait.
		if (customer.browseVisitsRemaining > 0)
		{
			customer.assignedItemPOIIndex = -1;
			customer.aiState = CustomerAIState::BrowserBrowsing;
			customer.poiWaitTimer = 0.0f;
			customer.SetAnimState(CustomerAnimState::Idle);
			return;
		}

		// Finished browsing: try to queue.
		int queueSlot = FindFreeQueueSlot(customerIndex);

		if (queueSlot >= 0)
		{
			AssignCustomerToQueueSlot(
				customerIndex,
				queueSlot,
				CustomerAIState::BrowserGoingToQueue
			);

			return;
		}

		// Queue full: do not stand at the same item.
		// Browse one more POI and try queue again later.
		customer.browseVisitsRemaining = 1;
		customer.assignedItemPOIIndex = -1;
		customer.aiState = CustomerAIState::BrowserBrowsing;
		customer.poiWaitTimer = 0.0f;
		customer.SetAnimState(CustomerAnimState::Idle);
		return;
	}


	case CustomerAIState::BrowserGoingToQueue:
	{
		if (!IsCustomerAtAssignedQueueSlot(customerIndex))
		{
			if (!customer.hasMoveTarget &&
				customer.assignedQueueSlotIndex >= 0)
			{
				StartCustomerRouteToPOI(
					customerIndex,
					customer.assignedQueueSlotIndex
				);
			}

			customer.poiWaitTimer = 0.10f;
			return;
		}

		customer.hasMoveTarget = false;
		customer.pathWaypoints.clear();
		customer.pathWaypointCursor = 0;

		customer.aiState = CustomerAIState::BrowserQueueing;
		customer.poiWaitTimer = 0.15f;

		break;
	}

	case CustomerAIState::BrowserQueueing:
	{
		if (customer.assignedQueueSlotIndex < 0)
		{
			int queueSlot = FindFreeQueueSlot(customerIndex);

			if (queueSlot >= 0)
			{
				AssignCustomerToQueueSlot(
					customerIndex,
					queueSlot,
					CustomerAIState::BrowserGoingToQueue
				);
			}
			else
			{
				customer.poiWaitTimer = 0.75f;
			}

			return;
		}

		if (!IsCustomerAtAssignedQueueSlot(customerIndex))
		{
			StartCustomerRouteToPOI(
				customerIndex,
				customer.assignedQueueSlotIndex
			);

			customer.poiWaitTimer = 0.15f;
			return;
		}

		int betterSlot = FindBetterQueueSlotForCustomer(customerIndex);

		if (betterSlot >= 0 &&
			betterSlot != customer.assignedQueueSlotIndex)
		{
			AssignCustomerToQueueSlot(
				customerIndex,
				betterSlot,
				CustomerAIState::BrowserGoingToQueue
			);

			return;
		}

		if (IsCustomerAtFrontQueueSlot(customerIndex))
		{
			customer.hasMoveTarget = false;
			customer.pathWaypoints.clear();
			customer.pathWaypointCursor = 0;

			if (!customer.tradeItemId.empty())
			{
				customer.dialogueScriptId = GetBuyerCounterScriptIdForItem(
					customer.tradeItemId
				);
			}
			else
			{
				customer.dialogueScriptId = "browser_counter_purchase";
			}

			customer.aiState = CustomerAIState::BrowserAtCounter;
			if (!IsCustomerInActiveDialogue(customerIndex))
			{
				customer.SetAnimState(CustomerAnimState::Idle);
			}

			// Stay here until player completes dialogue.
			customer.poiWaitTimer = 0.35f;
			return;
		}

		customer.poiWaitTimer = 0.25f;
		break;
	}

	case CustomerAIState::BrowserAtCounter:
	{
		if (customer.waitingForPlayerToReturnScannedItem)
		{
			customer.hasMoveTarget = false;
			customer.pathWaypoints.clear();
			customer.pathWaypointCursor = 0;

			if (IsCustomerScannedItemReturnedToCounterOffer(customerIndex))
			{
				CompleteBuyerReturnAfterScan(customerIndex);
				return;
			}

			if (!IsCustomerInActiveDialogue(customerIndex))
			{
				customer.SetAnimState(CustomerAnimState::Idle);
			}

			customer.poiWaitTimer = 0.20f;
			return;
		}

		if (customer.waitingForPlayerToScanItem)
		{
			customer.hasMoveTarget = false;
			customer.pathWaypoints.clear();
			customer.pathWaypointCursor = 0;

			if (!IsCustomerInActiveDialogue(customerIndex))
			{
				customer.SetAnimState(CustomerAnimState::Idle);
			}

			customer.poiWaitTimer = 0.20f;
			return;
		}

		if (customer.waitingForPlayerToTakeCounterItem)
		{
			customer.hasMoveTarget = false;
			customer.pathWaypoints.clear();
			customer.pathWaypointCursor = 0;

			if (!IsCustomerCounterItemStillOnCounter(customerIndex))
			{
				customer.waitingForPlayerToTakeCounterItem = false;
				customer.counterServiceCompleted = false;

				if (customer.role == CustomerRole::Seller)
				{
					customer.aiState = CustomerAIState::SellerSelling;
				}
				else
				{
					customer.aiState = CustomerAIState::BrowserPurchasing;
				}

				return;
			}

			if (!IsCustomerInActiveDialogue(customerIndex))
			{
				customer.SetAnimState(CustomerAnimState::Idle);
			}

			customer.poiWaitTimer = 0.35f;
			return;
		}

		// Do not auto-complete purchase here.
		// This state waits for player dialogue.
		if (!customer.tradeItemId.empty())
		{
			customer.dialogueScriptId = GetBuyerCounterScriptIdForItem(
				customer.tradeItemId
			);
		}
		else
		{
			customer.dialogueScriptId = "browser_counter_purchase";
		}
		customer.hasMoveTarget = false;
		customer.pathWaypoints.clear();
		customer.pathWaypointCursor = 0;

		if (!IsCustomerInActiveDialogue(customerIndex))
		{
			customer.SetAnimState(CustomerAnimState::Idle);
		}

		customer.poiWaitTimer = 0.35f;
		break;
	}

	case CustomerAIState::BrowserPurchasing:
	{
		int exit = FindExitPOI();

		customer.assignedQueueSlotIndex = -1;

		if (exit < 0)
		{
			customer.pendingDespawn = true;
			customer.aiState = CustomerAIState::Despawning;
			return;
		}

		customer.aiState = CustomerAIState::Leaving;
		StartCustomerRouteToPOI(customerIndex, exit);

		AdvanceQueueLine();

		break;
	}

	case CustomerAIState::SellerGoingToQueue:
	{
		if (customer.assignedQueueSlotIndex < 0)
		{
			int queueSlot = FindFreeQueueSlot(customerIndex);

			if (queueSlot < 0)
			{
				customer.poiWaitTimer = 0.75f;
				return;
			}

			AssignCustomerToQueueSlot(
				customerIndex,
				queueSlot,
				CustomerAIState::SellerGoingToQueue
			);

			return;
		}

		if (!IsCustomerAtAssignedQueueSlot(customerIndex))
		{
			if (!customer.hasMoveTarget)
			{
				StartCustomerRouteToPOI(
					customerIndex,
					customer.assignedQueueSlotIndex
				);
			}

			customer.poiWaitTimer = 0.10f;
			return;
		}

		customer.hasMoveTarget = false;
		customer.pathWaypoints.clear();
		customer.pathWaypointCursor = 0;

		customer.aiState = CustomerAIState::SellerQueueing;
		customer.poiWaitTimer = 0.15f;

		break;
	}

	case CustomerAIState::SellerQueueing:
	{
		if (customer.assignedQueueSlotIndex < 0)
		{
			int queueSlot = FindFreeQueueSlot(customerIndex);

			if (queueSlot >= 0)
			{
				AssignCustomerToQueueSlot(
					customerIndex,
					queueSlot,
					CustomerAIState::SellerGoingToQueue
				);
			}
			else
			{
				customer.poiWaitTimer = 0.75f;
			}

			return;
		}

		if (!IsCustomerAtAssignedQueueSlot(customerIndex))
		{
			StartCustomerRouteToPOI(
				customerIndex,
				customer.assignedQueueSlotIndex
			);

			customer.poiWaitTimer = 0.15f;
			return;
		}

		int betterSlot = FindBetterQueueSlotForCustomer(customerIndex);

		if (betterSlot >= 0 &&
			betterSlot != customer.assignedQueueSlotIndex)
		{
			AssignCustomerToQueueSlot(
				customerIndex,
				betterSlot,
				CustomerAIState::SellerGoingToQueue
			);

			return;
		}

		if (IsCustomerAtFrontQueueSlot(customerIndex))
		{
			customer.hasMoveTarget = false;
			customer.pathWaypoints.clear();
			customer.pathWaypointCursor = 0;

			AssignTradeItemToCustomer(customerIndex, true);

			customer.aiState = CustomerAIState::SellerAtCounter;

			if (!IsCustomerInActiveDialogue(customerIndex))
			{
				customer.SetAnimState(CustomerAnimState::Idle);
			}

			// Stay here until player completes dialogue.
			customer.poiWaitTimer = 0.35f;
			return;
		}

		customer.hasMoveTarget = false;
		if (!IsCustomerInActiveDialogue(customerIndex))
		{
			customer.SetAnimState(CustomerAnimState::Idle);
		}
		customer.poiWaitTimer = 0.25f;

		break;
	}

	case CustomerAIState::SellerAtCounter:
	{
		if (customer.waitingForPlayerToTakeCounterItem)
		{
			customer.hasMoveTarget = false;
			customer.pathWaypoints.clear();
			customer.pathWaypointCursor = 0;

			if (!IsCustomerCounterItemStillOnCounter(customerIndex))
			{
				customer.waitingForPlayerToTakeCounterItem = false;
				customer.counterServiceCompleted = false;

				if (customer.role == CustomerRole::Seller)
				{
					customer.aiState = CustomerAIState::SellerSelling;
				}
				else
				{
					customer.aiState = CustomerAIState::BrowserPurchasing;
				}

				return;
			}

			if (!IsCustomerInActiveDialogue(customerIndex))
			{
				customer.SetAnimState(CustomerAnimState::Idle);
			}

			customer.poiWaitTimer = 0.35f;
			return;
		}

		// Important:
		// Do NOT force seller_part1 every frame.
		// CompleteDialogueAndClose() may have advanced this customer to
		// seller_part2, seller_part3, seller_finished, or seller_refused.
		if (customer.dialogueScriptId.rfind("seller_", 0) != 0)
		{
			AssignTradeItemToCustomer(customerIndex, true);
		}

		customer.hasMoveTarget = false;
		customer.pathWaypoints.clear();
		customer.pathWaypointCursor = 0;


		if (customer.assignedQueueSlotIndex >= 0)
		{
			ApplyPOIFacingToCustomer(
				customerIndex,
				customer.assignedQueueSlotIndex
			);
		}

		if (!IsCustomerInActiveDialogue(customerIndex))
		{
			customer.SetAnimState(CustomerAnimState::Idle);
		}

		customer.poiWaitTimer = 0.35f;
		break;
	}

	case CustomerAIState::SellerSelling:
	{
		int exit = FindExitPOI();

		customer.assignedQueueSlotIndex = -1;

		if (exit < 0)
		{
			customer.pendingDespawn = true;
			customer.aiState = CustomerAIState::Despawning;
			return;
		}

		customer.aiState = CustomerAIState::Leaving;
		StartCustomerRouteToPOI(customerIndex, exit);

		AdvanceQueueLine();

		break;
	}

	case CustomerAIState::Leaving:
	{
		if (customer.hasMoveTarget)
		{
			return;
		}

		int exit = FindExitPOI();

		if (exit >= 0)
		{
			float distToExit = GetFlatDistanceXZ(
				customer.position,
				customerPOIs[exit].position
			);

			if (distToExit > fmaxf(customerPOIs[exit].radius, 0.35f))
			{
				StartCustomerRouteToPOI(customerIndex, exit);
				return;
			}
		}

		customer.pendingDespawn = true;
		customer.aiState = CustomerAIState::Despawning;
		break;
	}

	default:
		break;
	}
}

void Game::UpdateCustomerQueueState(int customerIndex)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	Customer& customer = customers[customerIndex];

	if (!IsQueueCustomerState(customer.aiState))
		return;

	// ----------------------------------------------------
	// Counter states:
	// Customer is already at the counter. Do not keep
	// re-routing or overwriting dialogue scripts here.
	// ----------------------------------------------------
	if (customer.aiState == CustomerAIState::BrowserAtCounter ||
		customer.aiState == CustomerAIState::SellerAtCounter ||
		customer.aiState == CustomerAIState::BrowserPurchasing ||
		customer.aiState == CustomerAIState::SellerSelling)
	{
		customer.hasMoveTarget = false;
		customer.pathWaypoints.clear();
		customer.pathWaypointCursor = 0;

		if (customer.assignedQueueSlotIndex >= 0)
		{
			ApplyPOIFacingToCustomer(
				customerIndex,
				customer.assignedQueueSlotIndex
			);
		}

		if (!IsCustomerInActiveDialogue(customerIndex))
		{
			customer.SetAnimState(CustomerAnimState::Idle);
		}

		customer.poiWaitTimer = 0.35f;
		return;
	}

	if (customer.assignedQueueSlotIndex < 0)
		return;

	// ----------------------------------------------------
	// Move customer to assigned queue slot.
	// ----------------------------------------------------
	if (!IsCustomerAtAssignedQueueSlot(customerIndex))
	{
		if (!customer.hasMoveTarget)
		{
			StartCustomerRouteToPOI(
				customerIndex,
				customer.assignedQueueSlotIndex
			);
		}

		return;
	}

	// ----------------------------------------------------
	// Move forward in queue if a better slot opens.
	// ----------------------------------------------------
	CustomerAIState nextGoingState =
		customer.role == CustomerRole::Seller
		? CustomerAIState::SellerGoingToQueue
		: CustomerAIState::BrowserGoingToQueue;

	int betterSlot = FindBetterQueueSlotForCustomer(customerIndex);

	if (betterSlot >= 0 &&
		betterSlot != customer.assignedQueueSlotIndex)
	{
		AssignCustomerToQueueSlot(
			customerIndex,
			betterSlot,
			nextGoingState
		);

		return;
	}

	// ----------------------------------------------------
	// Only queue slot 0 becomes the counter interaction.
	// Other queue customers should remain non-interactable.
	// ----------------------------------------------------
	if (!IsCustomerAtFrontQueueSlot(customerIndex))
	{
		if (!IsCustomerInActiveDialogue(customerIndex))
		{
			customer.SetAnimState(CustomerAnimState::Idle);
		}

		customer.poiWaitTimer = 0.35f;
		return;
	}

	// ----------------------------------------------------
	// Front queue slot reached: become counter customer.
	// Do NOT place the item here if you want it to happen
	// during dialogue. Only assign item + dialogue script.
	// ----------------------------------------------------
	customer.hasMoveTarget = false;
	customer.pathWaypoints.clear();
	customer.pathWaypointCursor = 0;

	if (customer.role == CustomerRole::Seller)
	{
		AssignTradeItemToCustomer(customerIndex, true);

		customer.aiState = CustomerAIState::SellerAtCounter;
	}
	else
	{
		if (customer.tradeItemId.empty())
		{
			AssignTradeItemToCustomer(customerIndex, false);
		}

		if (!customer.tradeItemId.empty())
		{
			customer.dialogueScriptId = GetBuyerCounterScriptIdForItem(
				customer.tradeItemId
			);
		}
		else
		{
			customer.dialogueScriptId = "browser_counter_purchase";
		}

		customer.aiState = CustomerAIState::BrowserAtCounter;
	}

	if (customer.assignedQueueSlotIndex >= 0)
	{
		ApplyPOIFacingToCustomer(
			customerIndex,
			customer.assignedQueueSlotIndex
		);
	}

	if (!IsCustomerInActiveDialogue(customerIndex))
	{
		customer.SetAnimState(CustomerAnimState::Idle);
	}

	customer.poiWaitTimer = 0.35f;
}

bool Game::CanTalkToCustomerNow(
	int customerIndex,
	std::string* blockedReason
) const
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return false;

	const Customer& customer = customers[customerIndex];

	if (customer.pendingDespawn)
		return false;

	if (customer.waitingForPlayerToTakeCounterItem)
	{
		if (IsCustomerCounterItemStillOnCounter(customerIndex))
		{
			if (blockedReason != nullptr)
			{
				*blockedReason = "Move the item off the counter first.";
			}

			return false;
		}

		return false;
	}
	if (customer.waitingForPlayerToScanItem)
	{
		if (blockedReason != nullptr)
		{
			*blockedReason = "Pick up the item, scan it, then return it to the counter.";
		}

		return false;
	}

	if (customer.waitingForPlayerToReturnScannedItem)
	{
		if (blockedReason != nullptr)
		{
			*blockedReason = "Return the scanned item to the counter.";
		}

		return false;
	}

	if (customer.counterServiceCompleted)
	{
		return false;
	}

	if (customer.aiState != CustomerAIState::SellerAtCounter &&
		customer.aiState != CustomerAIState::BrowserAtCounter)
	{
		return false;
	}

	// Only queue order 0 / front counter customer can be talked to.
	if (!IsCustomerAtFrontQueueSlot(customerIndex))
		return false;

	if (dialogueScripts.find(customer.dialogueScriptId) == dialogueScripts.end())
		return false;

	// If the counter item has not been placed yet, the counter must have
	// a free placement slot before dialogue can begin.
	if (!customer.counterItemPlaced)
	{
		ItemPlacementSpotKind kind = ItemPlacementSpotKind::CounterOffer;

		bool hasFreeSpot = false;

		for (int i = 0; i < (int)itemPlacementSpots.size(); i++)
		{
			const ItemPlacementSpot& spot = itemPlacementSpots[i];

			if (!spot.enabled)
				continue;

			if (!spot.allowCustomerPlace)
				continue;

			if (spot.kind != kind)
				continue;

			if (!IsPlacementSpotOccupiedByValidProp(
				i,
				customer.counterItemScenePropIndex
			))
			{
				hasFreeSpot = true;
				break;
			}
		}

		if (!hasFreeSpot)
		{
			if (blockedReason != nullptr)
			{
				std::string blockerText = "Move the item off the counter first.";

				for (int i = 0; i < (int)itemPlacementSpots.size(); i++)
				{
					const ItemPlacementSpot& spot = itemPlacementSpots[i];

					if (!spot.enabled)
						continue;

					if (!spot.allowCustomerPlace)
						continue;

					if (spot.kind != kind)
						continue;

					if (!IsPlacementSpotOccupiedByValidProp(
						i,
						customer.counterItemScenePropIndex
					))
					{
						continue;
					}

					int blockerIndex = spot.occupiedScenePropIndex;

					if (blockerIndex >= 0 &&
						blockerIndex < (int)sceneProps.size())
					{
						blockerText = TextFormat(
							"Move item from %s first: %s",
							spot.id.c_str(),
							sceneProps[blockerIndex].name.c_str()
						);
					}
					else
					{
						blockerText = TextFormat(
							"Move item from %s first.",
							spot.id.c_str()
						);
					}

					break;
				}

				*blockedReason = blockerText;
			}

			return false;
		}
	}

	return true;
}


void Game::RefreshLightShaderLocations(int index)
{
	if (index < 0 || index >= MAX_LIGHTS)
		return;

	Light& light = lights[index];

	light.enabledLoc = GetShaderLocation(
		pbrShader,
		TextFormat("lights[%i].enabled", index)
	);

	light.typeLoc = GetShaderLocation(
		pbrShader,
		TextFormat("lights[%i].type", index)
	);

	light.positionLoc = GetShaderLocation(
		pbrShader,
		TextFormat("lights[%i].position", index)
	);

	light.targetLoc = GetShaderLocation(
		pbrShader,
		TextFormat("lights[%i].target", index)
	);

	light.colorLoc = GetShaderLocation(
		pbrShader,
		TextFormat("lights[%i].color", index)
	);

	light.intensityLoc = GetShaderLocation(
		pbrShader,
		TextFormat("lights[%i].intensity", index)
	);

	light.rangeLoc = GetShaderLocation(
		pbrShader,
		TextFormat("lights[%i].range", index)
	);
}

int Game::AddLightToScene(const Light& lightTemplate)
{
	if (lightCount >= MAX_LIGHTS)
	{
		TraceLog(LOG_WARNING, "Cannot add light. MAX_LIGHTS reached.");
		return -1;
	}

	int newIndex = lightCount;

	lights[newIndex] = lightTemplate;

	RefreshLightShaderLocations(newIndex);

	lightCount++;

	SetShaderValue(
		pbrShader,
		lightCountLoc,
		&lightCount,
		SHADER_UNIFORM_INT
	);

	UpdateLight(lights[newIndex]);
	MarkShadowMapsDirty();

	return newIndex;
}

int Game::AddDefaultEditorLight()
{
	Camera3D activeCam = editMode ? editorCamera : camera;

	Vector3 forward = NormalizeSafe(
		Vector3Subtract(activeCam.target, activeCam.position)
	);

	Vector3 pos = Vector3Add(
		activeCam.position,
		Vector3Scale(forward, 2.0f)
	);

	pos.y += 0.4f;

	Light light{};

	light.name = "New Light " + std::to_string(lightCount + 1);
	light.enabled = 1;
	light.type = LIGHT_POINT;

	light.position = pos;
	light.target = Vector3Add(pos, { 0.0f, -1.0f, 0.0f });

	light.color[0] = 1.0f;
	light.color[1] = 1.0f;
	light.color[2] = 1.0f;
	light.color[3] = 1.0f;

	light.intensity = 3.0f;
	light.range = 6.0f;

	light.castsShadow = false;
	light.shadowRange = 6.0f;
	light.shadowBias = 0.005f;
	light.shadowStrength = 0.7f;
	light.shadowOrthoSize = 8.0f;
	light.shadowNear = 0.1f;
	light.shadowFar = 24.0f;
	light.shadowFovy = 45.0f;

	return AddLightToScene(light);
}

int Game::DuplicateLight(int sourceIndex)
{
	if (sourceIndex < 0 || sourceIndex >= lightCount)
		return -1;

	if (lightCount >= MAX_LIGHTS)
	{
		TraceLog(LOG_WARNING, "Cannot duplicate light. MAX_LIGHTS reached.");
		return -1;
	}

	Light copy = lights[sourceIndex];

	copy.name = lights[sourceIndex].name + " Copy";

	// Keep same relative/world position.
	// This means it will overlap perfectly until you move it.
	copy.position = lights[sourceIndex].position;
	copy.target = lights[sourceIndex].target;

	return AddLightToScene(copy);
}

int Game::GetSelectedLightIndex() const
{
	if (selectedEditorItemIndex < 0 ||
		selectedEditorItemIndex >= (int)editorItems.size())
	{
		return -1;
	}

	const EditorItem& item = editorItems[selectedEditorItemIndex];

	if (item.type != EditorItemType::Light)
		return -1;

	if (item.index < 0 || item.index >= lightCount)
		return -1;

	return item.index;
}

int Game::FindEditorItemIndexForLight(int lightIndex) const
{
	for (int i = 0; i < (int)editorItems.size(); i++)
	{
		const EditorItem& item = editorItems[i];

		if (item.type == EditorItemType::Light &&
			item.index == lightIndex)
		{
			return i;
		}
	}

	return -1;
}

void Game::DrawLightsEditorUI()
{
	ImGui::Text("Lights: %d / %d", lightCount, MAX_LIGHTS);

	if (lightCount >= MAX_LIGHTS)
	{
		ImGui::TextColored(
			ImVec4(1.0f, 0.45f, 0.25f, 1.0f),
			"Maximum light count reached."
		);
	}

	bool canAdd = lightCount < MAX_LIGHTS;

	if (!canAdd) ImGui::BeginDisabled();

	if (ImGui::Button("Add Light"))
	{
		PushUndoSnapshot();

		int newIndex = AddDefaultEditorLight();

		RebuildEditorItems();
		selectedEditorItemIndex = FindEditorItemIndexForLight(newIndex);
	}

	if (!canAdd) ImGui::EndDisabled();

	ImGui::SameLine();

	int selectedLightIndex = GetSelectedLightIndex();
	bool canDuplicate =
		selectedLightIndex >= 0 &&
		selectedLightIndex < lightCount &&
		lightCount < MAX_LIGHTS;

	if (!canDuplicate) ImGui::BeginDisabled();

	if (ImGui::Button("Duplicate Selected Light"))
	{
		PushUndoSnapshot();

		int newIndex = DuplicateLight(selectedLightIndex);

		RebuildEditorItems();
		selectedEditorItemIndex = FindEditorItemIndexForLight(newIndex);
	}

	if (!canDuplicate) ImGui::EndDisabled();

	ImGui::Separator();

	for (int i = 0; i < lightCount; i++)
	{
		std::string label = lights[i].name;

		if (label.empty())
			label = "Light " + std::to_string(i);

		label += "##LightList" + std::to_string(i);

		bool selected = selectedLightIndex == i;

		if (ImGui::Selectable(label.c_str(), selected))
		{
			selectedEditorItemIndex = FindEditorItemIndexForLight(i);
			selectedLightIndex = i;
		}
	}

	ImGui::Separator();

	selectedLightIndex = GetSelectedLightIndex();

	if (selectedLightIndex >= 0)
	{
		DrawLightInspector(selectedLightIndex);
	}
	else
	{
		ImGui::Text("Select a light to edit it.");
	}
}
void Game::DrawLightInspector(int lightIndex)
{
	if (lightIndex < 0 || lightIndex >= lightCount)
		return;

	ImGui::PushID(lightIndex);

	Light& light = lights[lightIndex];

	ImGui::Text("Editing Light %d", lightIndex);

	if (DrawEditableName("Name", light.name))
	{
	}
	PushUndoIfItemActivated();

	bool enabled = light.enabled != 0;

	if (ImGui::Checkbox("Enabled", &enabled))
	{
		light.enabled = enabled ? 1 : 0;
		UpdateLight(light);
		MarkShadowMapsDirty();
	}
	PushUndoIfItemActivated();

	const char* typeNames[] = {
		"Directional",
		"Point",
		"Spot"
	};

	int type = light.type;

	if (ImGui::Combo("Type", &type, typeNames, IM_ARRAYSIZE(typeNames)))
	{
		light.type = type;
		UpdateLight(light);
		MarkShadowMapsDirty();
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat3("Position", &light.position.x, 0.05f))
	{
		UpdateLight(light);
		MarkShadowMapsDirty();
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat3("Target", &light.target.x, 0.05f))
	{
		UpdateLight(light);
		MarkShadowMapsDirty();
	}
	PushUndoIfItemActivated();

	if (ImGui::ColorEdit4("Color", light.color))
	{
		UpdateLight(light);
		MarkShadowMapsDirty();
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 50.0f))
	{
		UpdateLight(light);
		MarkShadowMapsDirty();
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat("Range", &light.range, 0.05f, 0.1f, 100.0f))
	{
		UpdateLight(light);
		MarkShadowMapsDirty();
	}
	PushUndoIfItemActivated();

	ImGui::Separator();

	if (ImGui::CollapsingHeader("Shadow"))
	{
		if (ImGui::Checkbox("Casts Shadow", &light.castsShadow))
		{
			MarkShadowMapsDirty();
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat("Shadow Range", &light.shadowRange, 0.05f, 0.1f, 100.0f))
		{
			MarkShadowMapsDirty();
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat("Shadow Bias", &light.shadowBias, 0.0001f, 0.0f, 0.1f, "%.5f"))
		{
			MarkShadowMapsDirty();
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat("Shadow Strength", &light.shadowStrength, 0.05f, 0.0f, 5.0f))
		{
			MarkShadowMapsDirty();
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat("Shadow Ortho Size", &light.shadowOrthoSize, 0.05f, 0.1f, 100.0f))
		{
			MarkShadowMapsDirty();
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat("Shadow Near", &light.shadowNear, 0.01f, 0.01f, 20.0f))
		{
			MarkShadowMapsDirty();
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat("Shadow Far", &light.shadowFar, 0.1f, 0.1f, 200.0f))
		{
			MarkShadowMapsDirty();
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat("Shadow FOV", &light.shadowFovy, 0.5f, 1.0f, 179.0f))
		{
			MarkShadowMapsDirty();
		}
		PushUndoIfItemActivated();
	}
	ImGui::PopID();
}

Vector3 Game::FindApproachPositionNearPOI(int customerIndex, int poiIndex)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return { 0.0f, 0.0f, 0.0f };

	if (poiIndex < 0 || poiIndex >= (int)customerPOIs.size())
		return customers[customerIndex].position;

	const CustomerPOI& poi = customerPOIs[poiIndex];

	// Queue/counter/door POIs should use exact position.
	if (poi.kind == CustomerPOIKind::QueueSlot ||
		poi.kind == CustomerPOIKind::Counter ||
		poi.kind == CustomerPOIKind::Entry ||
		poi.kind == CustomerPOIKind::Exit)
	{
		return poi.position;
	}

	// BrowseItem and Generic should prefer the center.
	if (poi.kind == CustomerPOIKind::BrowseItem ||
		poi.kind == CustomerPOIKind::Generic)
	{
		Vector3 center = poi.position;
		center.y = 0.0f;

		if (!IsCustomerStaticBlockedAt(center) &&
			!IsCustomerDynamicBlockedAt(customerIndex, center))
		{
			return center;
		}
	}

	// Fallback ring search, but do NOT run A* here.
	// StartCustomerRouteToPOI() will run A* once after this function returns.
	Vector3 best = poi.position;
	float bestScore = FLT_MAX;
	bool found = false;

	const float startRadius = fmaxf(poi.radius, 0.45f);
	const float endRadius = 1.4f;
	const float radiusStep = 0.35f;

	for (float r = startRadius; r <= endRadius; r += radiusStep)
	{
		for (int a = 0; a < 12; a++)
		{
			float angle = ((float)a / 12.0f) * PI * 2.0f;

			Vector3 candidate = {
				poi.position.x + cosf(angle) * r,
				0.0f,
				poi.position.z + sinf(angle) * r
			};

			if (IsCustomerStaticBlockedAt(candidate))
				continue;

			if (IsCustomerDynamicBlockedAt(customerIndex, candidate))
				continue;

			float score =
				GetFlatDistanceXZ(customers[customerIndex].position, candidate) +
				GetFlatDistanceXZ(candidate, poi.position) * 0.25f;

			if (score < bestScore)
			{
				bestScore = score;
				best = candidate;
				found = true;
			}
		}

		if (found)
			break;
	}

	return best;
}
void Game::StartCustomerRouteToPOI(int customerIndex, int destinationIndex)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	if (destinationIndex < 0 || destinationIndex >= (int)customerPOIs.size())
		return;

	Customer& customer = customers[customerIndex];

	if (customer.pendingDespawn)
		return;

	if (customer.editorFrozen)
		return;

	if (customerRoutesBuiltThisFrame >= maxCustomerRoutesBuiltPerFrame)
	{
		customer.poiWaitTimer = 0.05f;
		return;
	}

	customerRoutesBuiltThisFrame++;

	if (customerNavGridDirty || !customerNavGrid.valid)
	{
		RebuildCustomerNavGrid();
	}

	Vector3 destination = FindApproachPositionNearPOI(
		customerIndex,
		destinationIndex
	);

	customer.destinationPOIIndex = destinationIndex;
	customer.targetPOIIndex = destinationIndex;
	customer.repathTimer = 0.0f;

	customer.pathWaypoints.clear();
	customer.pathWaypointCursor = 0;

	// Already close enough.
	if (GetFlatDistanceXZ(customer.position, destination) <= customer.moveStopDistance + 0.05f)
	{
		customer.hasMoveTarget = false;
		customer.currentPOIIndex = destinationIndex;
		customer.targetPOIIndex = -1;
		customer.destinationPOIIndex = -1;
		customer.targetPosition = customer.position;
		customer.SetAnimState(CustomerAnimState::Idle);
		return;
	}

	std::vector<Vector3> path = FindCustomerAStarPath(
		customerIndex,
		customer.position,
		destination,
		false
	);

	// Fallback for simple direct paths.
	if (path.empty() && !IsCustomerPathBlocked(customer.position, destination))
	{
		path.push_back(destination);
	}

	if (path.empty())
	{
		customer.hasMoveTarget = false;
		customer.pathWaypoints.clear();
		customer.pathWaypointCursor = 0;
		customer.poiWaitTimer = 0.35f;
		customer.SetAnimState(CustomerAnimState::Idle);

		TraceLog(
			LOG_WARNING,
			"Failed to route customer %i to POI %i",
			customerIndex,
			destinationIndex
		);

		return;
	}

	customer.pathWaypoints = path;

	int firstWaypoint = 0;

	if (customer.pathWaypoints.size() > 1 &&
		GetFlatDistanceXZ(customer.position, customer.pathWaypoints[0]) <= customer.moveStopDistance + 0.05f)
	{
		firstWaypoint = 1;
	}

	customer.pathWaypointCursor = firstWaypoint;

	customer.MoveTo(customer.pathWaypoints[customer.pathWaypointCursor]);

}

void Game::SaveCustomerPOIs()
{
	std::ofstream file("Data/customer_pois.txt");

	if (!file.is_open())
	{
		TraceLog(LOG_WARNING, "Failed to save customer POIs.");
		return;
	}

	file << "CUSTOMER_POIS_V3 " << customerPOIs.size() << "\n";

	for (const CustomerPOI& poi : customerPOIs)
	{
		file
			<< std::quoted(poi.id) << " "
			<< std::quoted(poi.group) << " "
			<< poi.position.x << " "
			<< poi.position.y << " "
			<< poi.position.z << " "
			<< poi.radius << " "
			<< poi.waitSecondsMin << " "
			<< poi.waitSecondsMax << " "
			<< poi.enabled << " "
			<< poi.stopPoint << " "
			<< poi.exclusive << " "
			<< poi.capacity << " "
			<< (int)poi.kind << " "
			<< poi.queueOrder << " "
			<< poi.useFacingDirection << " "
			<< poi.facingYawDeg
			<< "\n";
	}

	TraceLog(LOG_INFO, "Saved %i customer POIs.", (int)customerPOIs.size());
}
void Game::LoadCustomerPOIs()
{
	std::ifstream file("Data/customer_pois.txt");

	if (!file.is_open())
	{
		TraceLog(LOG_WARNING, "No customer POI file found.");
		return;
	}

	customerPOIs.clear();

	std::string tag;
	file >> tag;

	if (tag == "CUSTOMER_POIS_V3")
	{
		size_t poiCount = 0;
		file >> poiCount;

		for (size_t i = 0; i < poiCount; i++)
		{
			CustomerPOI poi;
			int kindValue = 0;

			file
				>> std::quoted(poi.id)
				>> std::quoted(poi.group)
				>> poi.position.x
				>> poi.position.y
				>> poi.position.z
				>> poi.radius
				>> poi.waitSecondsMin
				>> poi.waitSecondsMax
				>> poi.enabled
				>> poi.stopPoint
				>> poi.exclusive
				>> poi.capacity
				>> kindValue
				>> poi.queueOrder
				>> poi.useFacingDirection
				>> poi.facingYawDeg;

			if (file.fail())
				break;

			poi.kind = (CustomerPOIKind)kindValue;
			customerPOIs.push_back(poi);
		}
	}
	else if (tag == "CUSTOMER_POIS_V2")
	{
		if (tag == "CUSTOMER_POIS_V2")
		{
			size_t poiCount = 0;
			file >> poiCount;

			for (size_t i = 0; i < poiCount; i++)
			{
				CustomerPOI poi;
				int kindValue = 0;

				file
					>> std::quoted(poi.id)
					>> std::quoted(poi.group)
					>> poi.position.x
					>> poi.position.y
					>> poi.position.z
					>> poi.radius
					>> poi.waitSecondsMin
					>> poi.waitSecondsMax
					>> poi.enabled
					>> poi.stopPoint
					>> poi.exclusive
					>> poi.capacity
					>> kindValue
					>> poi.queueOrder;

				if (file.fail())
					break;

				poi.kind = (CustomerPOIKind)kindValue;
				customerPOIs.push_back(poi);
			}
		}
	}
	else
	{
		// Old format fallback.
		// First token was actually the first POI id.
		file.clear();
		file.seekg(0);

		while (true)
		{
			CustomerPOI poi;
			float oldWaitSeconds = 1.5f;

			file
				>> std::quoted(poi.id)
				>> std::quoted(poi.group)
				>> poi.position.x
				>> poi.position.y
				>> poi.position.z
				>> poi.radius
				>> oldWaitSeconds
				>> poi.enabled
				>> poi.stopPoint;

			if (file.fail())
				break;

			poi.waitSecondsMin = oldWaitSeconds;
			poi.waitSecondsMax = oldWaitSeconds;

			customerPOIs.push_back(poi);
		}
	}

	selectedCustomerPOI = -1;
	customerNavGridDirty = true;
	StopAllCustomerRoaming();

	TraceLog(LOG_INFO, "Loaded %i customer POIs.", (int)customerPOIs.size());
}
bool Game::IsCustomerPathBlocked(Vector3 from, Vector3 to) const
{
	Vector3 delta = Vector3Subtract(to, from);
	delta.y = 0.0f;

	float distance = Vector3Length(delta);

	if (distance <= 0.001f)
	{
		return IsCustomerStaticBlockedAt(to);
	}

	Vector3 dir = Vector3Scale(delta, 1.0f / distance);

	// Sample frequently enough that smoothing cannot cut corners.
	const float stepSize = 0.12f;
	int steps = (int)ceilf(distance / stepSize);

	for (int i = 0; i <= steps; i++)
	{
		float t = (float)i / (float)steps;

		Vector3 p = {
			from.x + delta.x * t,
			from.y,
			from.z + delta.z * t
		};

		if (IsCustomerStaticBlockedAt(p))
			return true;
	}

	return false;
}

int Game::PickNextCustomerDestinationPOI(const Customer& customer) const
{
	std::vector<int> validIndices;

	int customerIndex = -1;

	for (int i = 0; i < (int)customers.size(); i++)
	{
		if (&customers[i] == &customer)
		{
			customerIndex = i;
			break;
		}
	}

	for (int i = 0; i < (int)customerPOIs.size(); i++)
	{
		const CustomerPOI& poi = customerPOIs[i];

		if (!CanCustomerUsePOI(customer, poi, true))
			continue;

		if (i == customer.currentPOIIndex && customerPOIs.size() > 1)
			continue;

		if (!IsPOIAvailableForCustomer(customerIndex, i))
			continue;

		validIndices.push_back(i);
	}

	if (validIndices.empty())
		return -1;

	int randomIndex = GetRandomValue(0, (int)validIndices.size() - 1);
	return validIndices[randomIndex];
}

Model* Game::AcquireCustomerModelFromPool(const std::string& typeId)
{
	auto it = customerModelPools.find(typeId);

	if (it == customerModelPools.end())
	{
		TraceLog(LOG_WARNING, "No customer model pool for type: %s", typeId.c_str());
		return nullptr;
	}

	for (CustomerModelPoolItem& item : it->second)
	{
		if (!item.inUse && item.model != nullptr)
		{
			item.inUse = true;
			return item.model.get();
		}
	}

	TraceLog(
		LOG_WARNING,
		"Customer model pool exhausted for type: %s. Increase preload count.",
		typeId.c_str()
	);

	return nullptr;
}


void Game::UnloadCustomerModelPool()
{
	for (auto& pair : customerModelPools)
	{
		for (CustomerModelPoolItem& item : pair.second)
		{
			if (item.model && item.model->meshCount > 0)
			{
				UnloadModel(*item.model);
			}

			item.model.reset();
		}

		pair.second.clear();
	}

	customerModelPools.clear();
}
int Game::CountActiveCustomersByRole(CustomerRole role) const
{
	int count = 0;

	for (const Customer& customer : customers)
	{
		if (customer.pendingDespawn)
			continue;

		if (customer.role == role)
			count++;
	}

	return count;
}
void Game::PreloadCustomerModelPool()
{
	// Since any model can now become any role,
	// each model type should have enough pooled copies.
	int poolCount = maxCustomerCount;

	PreloadCustomerModelsForType("browser_male", poolCount);
	PreloadCustomerModelsForType("seller_male", poolCount);
	PreloadCustomerModelsForType("new_customer", poolCount);
}

void Game::PreloadCustomerModelsForType(const std::string& typeId, int count)
{
	auto it = customerTypes.find(typeId);

	if (it == customerTypes.end())
	{
		TraceLog(LOG_WARNING, "Cannot preload customer type. Missing type: %s", typeId.c_str());
		return;
	}

	CustomerType& type = it->second;

	if (type.modelPath.empty())
	{
		TraceLog(LOG_WARNING, "Cannot preload customer type. Empty model path: %s", typeId.c_str());
		return;
	}

	std::vector<CustomerModelPoolItem>& pool = customerModelPools[typeId];

	while ((int)pool.size() < count)
	{
		int current = (int)pool.size();

		float localT = count > 0
			? (float)current / (float)count
			: 1.0f;

		LoadingPulse(
			TextFormat("Preloading customer models: %s %i/%i",
				typeId.c_str(),
				current,
				count),
			Lerp(0.47f, 0.55f, localT),
			true
		);

		auto model = std::make_unique<Model>(
			LoadModel(type.modelPath.c_str())
		);

		if (model->meshCount <= 0)
		{
			TraceLog(LOG_WARNING, "Failed to preload customer model: %s", type.modelPath.c_str());
			continue;
		}

		ApplyAnimatedCustomerMaterial(*model);
		RepairAnimatedCustomerMaterialTextures(*model);

		CustomerModelPoolItem item;
		item.typeId = typeId;
		item.model = std::move(model);
		item.inUse = false;

		pool.push_back(std::move(item));

		LoadingPulse(
			TextFormat("Preloaded customer models: %s %i/%i",
				typeId.c_str(),
				(int)pool.size(),
				count),
			Lerp(0.47f, 0.55f, (float)pool.size() / (float)count),
			false
		);
	}

	TraceLog(
		LOG_INFO,
		"Preloaded customer pool: type=%s count=%i",
		typeId.c_str(),
		(int)pool.size()
	);
}


void Game::ReleaseCustomerModelToPool(Model* model)
{
	if (model == nullptr)
		return;

	for (auto& pair : customerModelPools)
	{
		for (CustomerModelPoolItem& item : pair.second)
		{
			if (item.model.get() == model)
			{
				item.inUse = false;
				return;
			}
		}
	}
}

void Game::ReleaseAllCustomerModelsToPool()
{
	for (auto& pair : customerModelPools)
	{
		for (CustomerModelPoolItem& item : pair.second)
		{
			item.inUse = false;
		}
	}
}

int Game::FindRandomPOIByKindForCustomer(
	CustomerPOIKind kind,
	const Customer& customer
) const
{
	std::vector<int> validIndices;

	int customerIndex = -1;

	for (int i = 0; i < (int)customers.size(); i++)
	{
		if (&customers[i] == &customer)
		{
			customerIndex = i;
			break;
		}
	}

	for (int i = 0; i < (int)customerPOIs.size(); i++)
	{
		const CustomerPOI& poi = customerPOIs[i];

		if (poi.kind != kind)
			continue;

		if (!CanCustomerUsePOI(customer, poi, true))
			continue;

		// Do not reselect the same POI.
		if (i == customer.currentPOIIndex)
			continue;

		if (i == customer.targetPOIIndex)
			continue;

		if (i == customer.destinationPOIIndex)
			continue;

		if (i == customer.assignedItemPOIIndex)
			continue;

		if (i == customer.assignedQueueSlotIndex)
			continue;

		// Respect exclusive/capacity.
		if (!IsPOIAvailableForCustomer(customerIndex, i))
			continue;

		validIndices.push_back(i);
	}

	if (validIndices.empty())
		return -1;

	int randomIndex = GetRandomValue(0, (int)validIndices.size() - 1);
	return validIndices[randomIndex];

}
int Game::FindDetourPOI(
	const Customer& customer,
	Vector3 from,
	Vector3 destination,
	int destinationIndex
) const
{
	int bestIndex = -1;
	float bestScore = FLT_MAX;

	for (int i = 0; i < (int)customerPOIs.size(); i++)
	{
		if (i == destinationIndex)
			continue;

		const CustomerPOI& poi = customerPOIs[i];

		if (!CanCustomerUsePOI(customer, poi, false))
			continue;

		if (IsCustomerPathBlocked(from, poi.position))
			continue;

		if (IsCustomerPathBlocked(poi.position, destination))
			continue;

		float score =
			Vector3Distance(from, poi.position) +
			Vector3Distance(poi.position, destination);

		if (score < bestScore)
		{
			bestScore = score;
			bestIndex = i;
		}
	}

	return bestIndex;
}

bool Game::CanCustomerUsePOI(
	const Customer& customer,
	const CustomerPOI& poi,
	bool destinationOnly
) const
{
	if (!poi.enabled)
		return false;

	if (destinationOnly && !poi.stopPoint)
		return false;

	// These are controlled by the customer AI state machine.
	// Normal roaming must not randomly pick them.
	if (poi.kind == CustomerPOIKind::QueueSlot)
		return false;

	if (poi.kind == CustomerPOIKind::Counter)
		return false;

	if (poi.kind == CustomerPOIKind::Entry)
		return false;

	if (poi.kind == CustomerPOIKind::Exit)
		return false;

	if (poi.group == "any")
		return true;

	return poi.group == customer.poiGroup;
}

bool Game::FindCustomerSpawnPositionNearPOI(
	int poiIndex,
	Vector3& outPosition
) const
{
	if (poiIndex < 0 || poiIndex >= (int)customerPOIs.size())
		return false;

	Vector3 base = customerPOIs[poiIndex].position;
	base.y = 0.0f;

	const float radii[] = {
		0.0f,
		0.55f,
		0.85f,
		1.20f,
		1.60f,
		2.00f,
		2.50f,
		3.00f
	};
	const Vector3 dirs[] = {
		{ 1.0f, 0.0f, 0.0f },
		{ -1.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f },
		{ 0.0f, 0.0f, -1.0f },
		{ 0.707f, 0.0f, 0.707f },
		{ -0.707f, 0.0f, 0.707f },
		{ 0.707f, 0.0f, -0.707f },
		{ -0.707f, 0.0f, -0.707f }
	};

	for (float r : radii)
	{
		for (Vector3 dir : dirs)
		{
			Vector3 candidate = Vector3Add(
				base,
				Vector3Scale(dir, r)
			);

			candidate.y = 0.0f;

			bool blocked =
				CustomerWouldHitSceneProps(candidate) ||
				CustomerWouldHitInstancedProps(candidate) ||
				CustomerWouldHitBlockout(candidate) ||
				CustomerWouldHitPlayer(candidate) ||
				CustomerWouldHitOtherCustomers(-1, candidate);

			if (blocked)
				continue;

			outPosition = candidate;
			return true;
		}
	}

	return false;
}

void Game::DespawnPendingCustomers()
{
	for (int i = (int)customers.size() - 1; i >= 0; i--)
	{
		if (!customers[i].pendingDespawn)
			continue;

		if (storeDayState == StoreDayState::Open ||
			storeDayState == StoreDayState::Closing)
		{
			customersCompletedToday++;

		}
		ReleaseCustomerModelToPool(customers[i].model);

		if (i >= 0 && i < (int)customerBodyIds.size())
		{
			if (physics && !customerBodyIds[i].IsInvalid())
			{
				physics->RemoveBody(customerBodyIds[i]);
			}

			customerBodyIds.erase(customerBodyIds.begin() + i);
		}

		customers.erase(customers.begin() + i);
	}
	UpdateStoreDayState();
}
bool Game::IsCustomerInActiveDialogue(int customerIndex) const
{
	return dialogueActive &&
		activeDialogueCustomerIndex == customerIndex;
}
void Game::UpdateCustomerPOINavigation(float dt)
{
	if (!customerRoamingEnabled)
		return;

	if (customerPOIs.empty())
		return;

	if (editMode)
		return;

	customerRoutesBuiltThisFrame = 0;

	for (int i = 0; i < (int)customers.size(); i++)
	{
		Customer& customer = customers[i];

		if (customer.editorFrozen)
		{
			customer.hasMoveTarget = false;
			customer.SetAnimState(CustomerAnimState::Idle);
			continue;
		}

		if (IsCustomerInActiveDialogue(i))
		{
			customer.hasMoveTarget = false;
			customer.pathWaypoints.clear();
			customer.pathWaypointCursor = 0;
			customer.movementPauseTimer = 0.0f;

			// Still update animation, but do not run collision response.
			// Otherwise standing close to the player can force Idle every frame.
			continue;
		}


		if (!customer.usePOINavigation)
			continue;

		if (dialogueActive && activeDialogueCustomerIndex == i)
		{
			customer.hasMoveTarget = false;
			customer.pathWaypoints.clear();
			customer.pathWaypointCursor = 0;

			// Do not call SetAnimState(Idle) here.
			// Dialogue owns the customer's animation while talking.
			continue;
		}

		// ----------------------------------------------------
		// 1. First: progress existing A* waypoint route.
		// This must happen before UpdateCustomerAI().
		// ----------------------------------------------------
		if (customer.hasMoveTarget)
		{
			float distanceToWaypoint = GetFlatDistanceXZ(
				customer.position,
				customer.targetPosition
			);

			if (distanceToWaypoint <= customer.moveStopDistance + 0.05f)
			{
				customer.pathWaypointCursor++;

				if (customer.pathWaypointCursor < (int)customer.pathWaypoints.size())
				{
					customer.MoveTo(customer.pathWaypoints[customer.pathWaypointCursor]);
				}
				else
				{
					customer.hasMoveTarget = false;
					customer.SetAnimState(CustomerAnimState::Idle);

					int arrivedPOI = customer.destinationPOIIndex;

					if (arrivedPOI >= 0 &&
						arrivedPOI < (int)customerPOIs.size())
					{
						customer.currentPOIIndex = arrivedPOI;

						// New: face shelf/counter/queue direction when arriving.
						ApplyPOIFacingToCustomer(i, arrivedPOI);

						if (customer.aiState == CustomerAIState::None)
						{
							customer.poiWaitTimer = GetRandomPOIWaitSeconds(
								customerPOIs[arrivedPOI]
							);
						}
						else
						{
							customer.poiWaitTimer = 0.0f;
						}
					}

					customer.targetPOIIndex = -1;
					customer.destinationPOIIndex = -1;
					customer.pathWaypoints.clear();
					customer.pathWaypointCursor = 0;
				}
			}

			continue;
		}

		// ----------------------------------------------------
		// 2. Then: state-machine AI.
		// Queue states must come through here too.
		// ----------------------------------------------------
		if (customer.aiState != CustomerAIState::None)
		{
			UpdateCustomerAI(i, dt);
			continue;
		}

		// ----------------------------------------------------
		// 3. Normal roaming-only behaviour.
		// This should not be used by queue customers.
		// ----------------------------------------------------
		if (customer.poiWaitTimer > 0.0f)
		{
			customer.poiWaitTimer -= dt;
			continue;
		}

		if (customer.destinationPOIIndex >= 0 &&
			customer.destinationPOIIndex < (int)customerPOIs.size() &&
			customer.pathWaypoints.empty() &&
			!customer.hasMoveTarget)
		{
			StartCustomerRouteToPOI(i, customer.destinationPOIIndex);
			continue;
		}

		int nextDestination = PickNextCustomerDestinationPOI(customer);

		if (nextDestination < 0)
			continue;

		StartCustomerRouteToPOI(i, nextDestination);
	}
}
void Game::AdvanceQueueLine()
{
	std::vector<int> queueCustomers;

	for (int i = 0; i < (int)customers.size(); i++)
	{
		Customer& customer = customers[i];

		if (!IsQueueCustomerState(customer.aiState))
			continue;

		if (IsCounterCustomerState(customer.aiState))
			continue;

		if (customer.assignedQueueSlotIndex < 0)
			continue;

		if (customer.pendingDespawn)
			continue;

		queueCustomers.push_back(i);
	}

	std::sort(
		queueCustomers.begin(),
		queueCustomers.end(),
		[this](int a, int b)
		{
			int orderA = GetPOIQueueOrder(customers[a].assignedQueueSlotIndex);
			int orderB = GetPOIQueueOrder(customers[b].assignedQueueSlotIndex);
			return orderA < orderB;
		}
	);

	for (int customerIndex : queueCustomers)
	{
		Customer& customer = customers[customerIndex];

		int betterSlot = FindBetterQueueSlotForCustomer(customerIndex);

		if (betterSlot >= 0 &&
			betterSlot != customer.assignedQueueSlotIndex)
		{
			CustomerAIState goingState =
				customer.role == CustomerRole::Seller
				? CustomerAIState::SellerGoingToQueue
				: CustomerAIState::BrowserGoingToQueue;

			AssignCustomerToQueueSlot(
				customerIndex,
				betterSlot,
				goingState
			);
		}
	}
}

void Game::OnCustomerDialogueCompleted(
	int customerIndex,
	const std::string& scriptId
)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	bool isBuyerPurchaseScript =
		scriptId == "browser_counter_purchase" ||
		scriptId.rfind("buyer_", 0) == 0;

	bool isSellerFinalScript =
		scriptId == "seller_finished" ||
		scriptId == "seller_refused" ||
		scriptId.find("_part3") != std::string::npos;

	if (isBuyerPurchaseScript || isSellerFinalScript)
	{
		CompleteCounterCustomerService(customerIndex);
	}
}

void Game::CustomerTakeCounterItem(int customerIndex)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	Customer& customer = customers[customerIndex];

	int propIndex = customer.counterItemScenePropIndex;

	if (propIndex >= 0 && propIndex < (int)sceneProps.size())
	{
		SceneProp& prop = sceneProps[propIndex];

		ClearScenePropPlacementSpot(propIndex);

		if (physics && !prop.bodyId.IsInvalid())
		{
			physics->RemoveBody(prop.bodyId);
			prop.bodyId = JPH::BodyID();
		}

		// Hide/remove from interaction. We keep it in the vector to avoid
		// invalidating scene prop indices during gameplay.
		prop.visible = false;
		prop.canPickup = false;
		prop.hasCollision = false;
		prop.blocksPlayer = false;
		prop.simulatePhysics = false;
		prop.syncFromPhysics = false;
		prop.editLockPhysics = true;

		prop.currentPlacementSpotIndex = -1;
		prop.position = { 0.0f, -9999.0f, 0.0f };
	}

	customer.carriedItemScenePropIndex = propIndex;
	customer.counterItemScenePropIndex = -1;
	customer.counterItemPlaced = false;
	customer.waitingForPlayerToTakeCounterItem = false;

	RebuildItemPlacementSpotOccupancy();
	MarkShadowMapsDirty();
}

void Game::CompleteCounterCustomerService(int customerIndex)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	Customer& customer = customers[customerIndex];

	if (customer.aiState != CustomerAIState::BrowserAtCounter &&
		customer.aiState != CustomerAIState::SellerAtCounter)
	{
		return;
	}

	customer.hasMoveTarget = false;
	customer.pathWaypoints.clear();
	customer.pathWaypointCursor = 0;

	// Buyer flow:
	// Player scanned/sold the item, then buyer takes item back and leaves.
	if (customer.role == CustomerRole::Browser)
	{
		customer.counterServiceCompleted = true;

		// Buyer has placed item on CounterOffer and now waits
		// for the player to scan it.
		customer.waitingForPlayerToScanItem = true;
		customer.waitingForPlayerToReturnScannedItem = false;
		customer.waitingForPlayerToTakeCounterItem = false;

		customer.hasMoveTarget = false;
		customer.pathWaypoints.clear();
		customer.pathWaypointCursor = 0;

		customer.SetAnimState(CustomerAnimState::Idle);
		return;
	}

	// Seller flow:
	// If the player bought the seller's item, the item should stay on
	// the counter until the player takes it.
	if (customer.role == CustomerRole::Seller)
	{
		customer.counterServiceCompleted = true;

		if (IsCustomerCounterItemStillOnCounter(customerIndex))
		{
			customer.waitingForPlayerToTakeCounterItem = true;
			customer.SetAnimState(CustomerAnimState::Idle);
			return;
		}

		// Seller refused/cancelled/took item back.
		customer.waitingForPlayerToTakeCounterItem = false;
		customer.counterServiceCompleted = false;
		customer.SetAnimState(CustomerAnimState::Idle);
		customer.aiState = CustomerAIState::SellerSelling;

		return;
	}
}
bool Game::IsCustomerScannedItemReturnedToCounterOffer(int customerIndex) const
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return false;

	const Customer& customer = customers[customerIndex];

	int propIndex = customer.counterItemScenePropIndex;

	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return false;

	const SceneProp& prop = sceneProps[propIndex];

	if (!prop.scannedForCustomer)
		return false;

	int spotIndex = prop.currentPlacementSpotIndex;

	if (spotIndex < 0 || spotIndex >= (int)itemPlacementSpots.size())
		return false;

	const ItemPlacementSpot& spot = itemPlacementSpots[spotIndex];

	if (spot.kind != ItemPlacementSpotKind::CounterOffer)
		return false;

	if (spot.occupiedScenePropIndex != propIndex)
		return false;

	return true;
}
bool Game::IsCustomerCounterItemStillOnCounter(int customerIndex) const
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return false;

	const Customer& customer = customers[customerIndex];

	int propIndex = customer.counterItemScenePropIndex;

	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return false;

	const SceneProp& prop = sceneProps[propIndex];

	int spotIndex = prop.currentPlacementSpotIndex;

	if (spotIndex < 0 || spotIndex >= (int)itemPlacementSpots.size())
		return false;

	const ItemPlacementSpot& spot = itemPlacementSpots[spotIndex];

	bool isCounterSpot =
		spot.kind == ItemPlacementSpotKind::CounterOffer ||
		spot.kind == ItemPlacementSpotKind::CounterScan;

	if (!isCounterSpot)
		return false;

	if (spot.occupiedScenePropIndex != propIndex)
		return false;

	return true;
}

std::vector<int> Game::FindPOIsByKind(Game::CustomerPOIKind kind) const
{
	std::vector<int> result;

	for (int i = 0; i < (int)customerPOIs.size(); i++)
	{
		const CustomerPOI& poi = customerPOIs[i];

		if (!poi.enabled)
			continue;

		if (poi.kind == kind)
			result.push_back(i);
	}

	return result;
}
int Game::FindExitPOI() const
{
	int exit = FindPOIByKind(CustomerPOIKind::Exit);

	if (exit >= 0)
		return exit;

	// Temporary fallback:
	// use Entry as Exit if you only have one door.
	return FindPOIByKind(CustomerPOIKind::Entry);
}
int Game::FindRandomPOIByKind(Game::CustomerPOIKind kind) const
{
	std::vector<int> pois = FindPOIsByKind(kind);

	if (pois.empty())
		return -1;

	int randomIndex = GetRandomValue(0, (int)pois.size() - 1);
	return pois[randomIndex];
}
int Game::GetPOIQueueOrder(int poiIndex) const
{
	if (poiIndex < 0 || poiIndex >= (int)customerPOIs.size())
		return 9999;

	return customerPOIs[poiIndex].queueOrder;
}
bool Game::IsQueueSlotUsedByOtherCustomer(int slotIndex, int customerIndex) const
{
	for (int i = 0; i < (int)customers.size(); i++)
	{
		if (i == customerIndex)
			continue;

		const Customer& other = customers[i];

		if (other.assignedQueueSlotIndex != slotIndex)
			continue;

		if (!IsQueueCustomerState(other.aiState))
			continue;

		if (other.pendingDespawn)
			continue;

		return true;
	}

	return false;
}

void Game::AssignCustomerToQueueSlot(
	int customerIndex,
	int queueSlotIndex,
	CustomerAIState movingState
)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	if (queueSlotIndex < 0 || queueSlotIndex >= (int)customerPOIs.size())
		return;

	Customer& customer = customers[customerIndex];

	customer.assignedQueueSlotIndex = queueSlotIndex;
	customer.destinationPOIIndex = queueSlotIndex;
	customer.targetPOIIndex = queueSlotIndex;

	customer.aiState = movingState;

	customer.poiWaitTimer = 0.0f;
	customer.movementPauseTimer = 0.0f;

	customer.pathWaypoints.clear();
	customer.pathWaypointCursor = 0;

	StartCustomerRouteToPOI(customerIndex, queueSlotIndex);
}

bool Game::IsCounterBusy(int selfCustomerIndex) const
{
	int counter = FindPOIByKind(CustomerPOIKind::Counter);

	if (counter < 0)
		return true;

	for (int i = 0; i < (int)customers.size(); i++)
	{
		if (i == selfCustomerIndex)
			continue;

		const Customer& other = customers[i];

		if (other.pendingDespawn)
			continue;

		if (other.currentPOIIndex == counter)
			return true;

		if (other.destinationPOIIndex == counter)
			return true;

		if (other.aiState == CustomerAIState::BrowserAtCounter ||
			other.aiState == CustomerAIState::BrowserPurchasing ||
			other.aiState == CustomerAIState::SellerAtCounter ||
			other.aiState == CustomerAIState::SellerSelling)
		{
			return true;
		}
	}

	return false;
}

bool Game::IsQueueCustomerState(CustomerAIState state) const
{
	return
		state == CustomerAIState::BrowserGoingToQueue ||
		state == CustomerAIState::BrowserQueueing ||
		state == CustomerAIState::BrowserAtCounter ||
		state == CustomerAIState::BrowserPurchasing ||
		state == CustomerAIState::SellerGoingToQueue ||
		state == CustomerAIState::SellerQueueing ||
		state == CustomerAIState::SellerAtCounter ||
		state == CustomerAIState::SellerSelling;
}
bool Game::IsCounterCustomerState(CustomerAIState state) const
{
	return
		state == CustomerAIState::BrowserAtCounter ||
		state == CustomerAIState::BrowserPurchasing ||
		state == CustomerAIState::SellerAtCounter ||
		state == CustomerAIState::SellerSelling;
}
bool Game::IsCustomerAtAssignedQueueSlot(int customerIndex) const
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return false;

	const Customer& customer = customers[customerIndex];

	if (customer.assignedQueueSlotIndex < 0 ||
		customer.assignedQueueSlotIndex >= (int)customerPOIs.size())
	{
		return false;
	}

	const CustomerPOI& slot = customerPOIs[customer.assignedQueueSlotIndex];

	float dist = GetFlatDistanceXZ(customer.position, slot.position);

	return dist <= fmaxf(slot.radius, 0.18f);
}
bool Game::IsCustomerAtFrontQueueSlot(int customerIndex) const
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return false;

	const Customer& customer = customers[customerIndex];

	if (customer.assignedQueueSlotIndex < 0)
		return false;

	return GetPOIQueueOrder(customer.assignedQueueSlotIndex) == 0;
}
int Game::FindFreeQueueSlot(int customerIndex) const
{
	std::vector<int> slots = FindPOIsByKind(CustomerPOIKind::QueueSlot);

	std::sort(
		slots.begin(),
		slots.end(),
		[this](int a, int b)
		{
			return GetPOIQueueOrder(a) < GetPOIQueueOrder(b);
		}
	);

	for (int slot : slots)
	{
		if (!IsQueueSlotUsedByOtherCustomer(slot, customerIndex))
			return slot;
	}

	return -1;
}
int Game::FindBetterQueueSlotForCustomer(int customerIndex) const
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return -1;

	const Customer& customer = customers[customerIndex];

	if (customer.assignedQueueSlotIndex < 0)
		return FindFreeQueueSlot(customerIndex);

	int currentOrder = GetPOIQueueOrder(customer.assignedQueueSlotIndex);

	std::vector<int> slots = FindPOIsByKind(CustomerPOIKind::QueueSlot);

	int bestSlot = customer.assignedQueueSlotIndex;
	int bestOrder = -999999;

	for (int slot : slots)
	{
		int order = GetPOIQueueOrder(slot);

		// Only move toward the counter.
		if (order >= currentOrder)
			continue;

		// Pick the closest lower slot, e.g. 2 -> 1, not 2 -> 0.
		if (order <= bestOrder)
			continue;

		if (IsQueueSlotUsedByOtherCustomer(slot, customerIndex))
			continue;

		bestSlot = slot;
		bestOrder = order;
	}

	return bestSlot;
}
bool Game::TryCustomerSidestep(int customerIndex)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return false;

	Customer& customer = customers[customerIndex];

	if (!customer.hasMoveTarget)
		return false;

	Vector3 toTarget = Vector3Subtract(customer.targetPosition, customer.position);
	toTarget.y = 0.0f;

	if (Vector3Length(toTarget) <= 0.001f)
		return false;

	Vector3 forward = Vector3Normalize(toTarget);

	Vector3 right = {
		forward.z,
		0.0f,
		-forward.x
	};

	const float sideDistances[] = {
		0.65f,
		1.0f,
		1.35f
	};

	for (float sideDistance : sideDistances)
	{
		Vector3 candidates[2] = {
			Vector3Add(customer.position, Vector3Scale(right, sideDistance)),
			Vector3Subtract(customer.position, Vector3Scale(right, sideDistance))
		};

		for (Vector3 candidate : candidates)
		{
			candidate.y = 0.0f;

			if (IsCustomerStaticBlockedAt(candidate))
				continue;

			if (IsCustomerDynamicBlockedAt(customerIndex, candidate))
				continue;

			// Make sure the sidestep is reachable from the current position.
			std::vector<Vector3> sidePath = FindCustomerAStarPath(
				customerIndex,
				customer.position,
				candidate,
				false
			);

			if (sidePath.empty())
				continue;

			// Insert the sidestep before continuing to the existing destination.
			customer.pathWaypoints.clear();
			customer.pathWaypoints.push_back(candidate);

			if (customer.destinationPOIIndex >= 0 &&
				customer.destinationPOIIndex < (int)customerPOIs.size())
			{
				std::vector<Vector3> pathToDestination = FindCustomerAStarPath(
					customerIndex,
					candidate,
					customerPOIs[customer.destinationPOIIndex].position,
					false
				);

				for (Vector3 p : pathToDestination)
				{
					customer.pathWaypoints.push_back(p);
				}
			}
			else
			{
				customer.pathWaypoints.push_back(customer.targetPosition);
			}

			customer.pathWaypointCursor = 0;
			customer.MoveTo(customer.pathWaypoints[0]);

			return true;
		}
	}

	return false;
}

void Game::UpdateCustomerDynamicAvoidance(float dt)
{
	for (int i = 0; i < (int)customers.size(); i++)
	{
		Customer& customer = customers[i];

		if (customer.editorFrozen)
			continue;

		if (!customer.hasMoveTarget)
			continue;

		if (customer.movementPauseTimer > 0.0f)
			continue;

		Vector3 toTarget = Vector3Subtract(customer.targetPosition, customer.position);
		toTarget.y = 0.0f;

		if (Vector3Length(toTarget) <= 0.001f)
			continue;

		Vector3 dir = Vector3Normalize(toTarget);

		Vector3 probePosition = Vector3Add(
			customer.position,
			Vector3Scale(dir, 0.65f)
		);

		if (IsCustomerDynamicBlockedAt(i, probePosition))
		{
			if (TryCustomerSidestep(i))
			{
				customer.repathTimer = 0.0f;
				continue;
			}

			// If no sidestep is possible, wait briefly.
			customer.movementPauseTimer =
				0.25f + (float)GetRandomValue(0, 25) / 100.0f;

			customer.repathTimer += dt;

			if (customer.repathTimer >= customerRepathInterval &&
				customer.destinationPOIIndex >= 0)
			{
				customer.repathTimer = 0.0f;
				StartCustomerRouteToPOI(i, customer.destinationPOIIndex);
			}
		}
		else
		{
			customer.repathTimer = 0.0f;
		}
	}
}

void Game::UpdateLight(const Light& light) const
{
	SetShaderValue(pbrShader, light.enabledLoc, &light.enabled, SHADER_UNIFORM_INT);
	SetShaderValue(pbrShader, light.typeLoc, &light.type, SHADER_UNIFORM_INT);

	float position[3] = { light.position.x, light.position.y, light.position.z };
	float target[3] = { light.target.x, light.target.y, light.target.z };

	SetShaderValue(pbrShader, light.positionLoc, position, SHADER_UNIFORM_VEC3);
	SetShaderValue(pbrShader, light.targetLoc, target, SHADER_UNIFORM_VEC3);
	SetShaderValue(pbrShader, light.colorLoc, (void*)light.color, SHADER_UNIFORM_VEC4);
	SetShaderValue(pbrShader, light.intensityLoc, (void*)&light.intensity, SHADER_UNIFORM_FLOAT);
	if (light.rangeLoc >= 0)
	{
		SetShaderValue(
			pbrShader,
			light.rangeLoc,
			(void*)&light.range,
			SHADER_UNIFORM_FLOAT
		);
	}
}

void Game::RecreatePhysicsWorld()
{
	// Remove customer bodies while they still belong to the old physics world.
	RemoveCustomerBodies();
	customerBodyIds.clear();

	/*
		Quaternion currentBoxRot = physicsBoxSavedRot;



	if (hasTestDynamicBox && physics && !testDynamicBox.IsInvalid())
	{
		currentBoxPos = physics->GetBodyPosition(testDynamicBox);
		currentBoxRot = physics->GetBodyRotation(testDynamicBox);
	}

	*/
	Vector3 currentBoxPos = physicsBoxSavedPos;


	Vector3 currentPlayerPos = player.m_pos;
	Vector3 currentPlayerVel = player.m_velocity;
	CaptureLiveScenePropPhysicsTransforms();

	physics = std::make_unique<PhysicsWorld>();

	// Important:
	// Do NOT call ApplyImportedCollisionNamingRules() here.
	// Rebuilding physics should respect the currently edited/saved collision states.
	PreparePickupScenePropsForPhysics();
	UpdateScenePropWorldTransforms();
	ApplyImportedEditorTransformsToRuntime();

	BuildStaticBodiesFromBlockout();
	BuildStaticBodiesFromSceneProps();
	BuildStaticBodiesFromInstancedProps();

	pendingPostLoadPhysicsRestore = true;

	player.m_pos = currentPlayerPos;

	CreateVirtualPlayer();

	if (playerCharacter)
	{
		playerCharacter->SetLinearVelocity(
			JPH::Vec3(
				currentPlayerVel.x,
				currentPlayerVel.y,
				currentPlayerVel.z
			)
		);

		SyncPlayerFromVirtual();
	}

	testDynamicBox = JPH::BodyID();
	hasTestDynamicBox = false;
	isHoldingBox = false;
	heldBody = JPH::BodyID();
	heldScenePropIndex = -1;
	hasHeldBody = false;

	blockoutDirty = false;
	customerNavGridDirty = true;

	// Build customer bodies only after the new physics world is complete.
	BuildCustomerBodies();
	MarkInteractableScenePropCacheDirty();
	MarkShadowMapsDirty();
}

void Game::PurgeRuntimeCustomerItemSceneProps()
{
	int removed = 0;

	auto adjustIndexAfterErase = [](int& value, int removedIndex)
		{
			if (value == removedIndex)
				value = -1;
			else if (value > removedIndex)
				value--;
		};

	for (int i = (int)sceneProps.size() - 1; i >= 0; i--)
	{
		SceneProp& prop = sceneProps[i];

		if (!ShouldPurgeRuntimeCustomerItemProp(prop))
			continue;

		if (hasHeldBody && heldScenePropIndex == i)
		{
			StopHoldingBody();
		}

		if (physics && !prop.bodyId.IsInvalid())
		{
			physics->RemoveBody(prop.bodyId);
			prop.bodyId = JPH::BodyID();
		}

		for (Customer& customer : customers)
		{
			adjustIndexAfterErase(customer.carriedItemScenePropIndex, i);
			adjustIndexAfterErase(customer.counterItemScenePropIndex, i);

			if (customer.carriedItemScenePropIndex < 0)
			{
				customer.hasPickedItem = false;
			}

			if (customer.counterItemScenePropIndex < 0)
			{
				customer.counterItemPlaced = false;
				customer.counterItemPlacementAttempted = false;
			}
		}

		adjustIndexAfterErase(selectedScenePropIndex, i);
		adjustIndexAfterErase(targetedScenePropIndex, i);
		adjustIndexAfterErase(heldScenePropIndex, i);
		adjustIndexAfterErase(placementPreviewPropIndex, i);

		sceneProps.erase(sceneProps.begin() + i);
		removed++;
	}

	RepairScenePropHierarchyLinks();
	RebuildItemPlacementSpotOccupancy();
	UpdateScenePropWorldTransforms();
	RecreatePhysicsWorld();

	MarkShadowTextureBindingsDirty(false);

	customerNavGridDirty = true;
	MarkShadowMapsDirty();

	TraceLog(LOG_INFO, "Purged %i runtime customer item scene props.", removed);
}

bool Game::ShouldPurgeRuntimeCustomerItemProp(const SceneProp& prop) const
{
	if (prop.placedByCustomer)
		return true;

	if (prop.owningCustomerIndex >= 0)
		return true;

	// Only delete name-only customer items if they are not imported GLB scene props.
	// Imported GLB props with corrupted names should be restored, not deleted.
	if (!prop.importedFromGlbScene &&
		StartsWithText(prop.name, "Customer Item - "))
	{
		return true;
	}

	return false;
}


void Game::DrawSceneEditorUI()
{
#if !GAME_ENABLE_EDITOR
	return;
#endif

	RebuildEditorItems();

	ImGui::Begin("Scene Editor");

	if (ImGui::Button(editMode ? "Exit Edit Mode" : "Enter Edit Mode"))
	{
		if (editMode) ExitEditMode();
		else EnterEditMode();
	}

	ImGui::Text("Edit cam: RMB look, WASD move, Q/E up/down, Shift speed");

	if (ImGui::Button("Save Scene"))
	{
		CaptureLiveScenePropPhysicsTransforms();
		UpdateScenePropWorldTransforms();
		ApplyImportedEditorTransformsToRuntime();
		SaveSceneState();
	}
	ImGui::SameLine();

	if (ImGui::Button("Load Scene"))
	{
		if (LoadSceneState())
		{
			UpdateScenePropWorldTransforms();

			MarkShadowTextureBindingsDirty(false);

			RecreatePhysicsWorld();
			physics->SetBodyRotation(testDynamicBox, physicsBoxSavedRot);
			MarkShadowMapsDirty();
		}
	}



	ImGui::SameLine();

	if (ImGui::Button("Test Seller Item"))
	{
		int customerIndex = selectedCustomerIndex >= 0 ? selectedCustomerIndex : 0;
		CreateCounterOfferItemForCustomer(customerIndex);
	}

	ImGui::SameLine();

	if (ImGui::Button("Rebuild Physics"))
	{
		CaptureLiveScenePropPhysicsTransforms();
		UpdateScenePropWorldTransforms();
		RecreatePhysicsWorld();
	}

	ImGui::SameLine();

	if (ImGui::Button("Re-apply Collision Naming"))
	{
		PushUndoSnapshot();

		ApplyImportedCollisionNamingRules();
		UpdateScenePropWorldTransforms();
		RecreatePhysicsWorld();

		customerNavGridDirty = true;
		MarkShadowMapsDirty();
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset Record Player Visual Pose"))
	{
		PushUndoSnapshot();

		// Use true if you want it to stay visually reset.
		// If false, autoplay will immediately put the hand back onto the record.
		ResetRecordPlayerVisualPose(true);
	}

	ImGui::SameLine();

	if (ImGui::Button("Restore Imported Names"))
	{
		PushUndoSnapshot();

		RestoreCorruptedImportedScenePropNames();
		UpdateScenePropWorldTransforms();
		RecreatePhysicsWorld();

		customerNavGridDirty = true;
		MarkShadowMapsDirty();
	}

	ImGui::SameLine();
	if (ImGui::Button("Purge Runtime Customer Items"))
	{
		PushUndoSnapshot();

		PurgeRuntimeCustomerItemSceneProps();
	}
	ImGui::Separator();
	if (ImGui::CollapsingHeader("Debug Draw"))
	{
		ImGui::Checkbox("Draw Physics / Collider Bounds", &drawPhysicsDebugBounds);
		ImGui::Checkbox("Draw Selected Prop Render Bounds", &drawSelectedScenePropBounds);
		ImGui::Checkbox("Draw Customer POI / Placement Debug", &drawEditorPointDebug);
		ImGui::Checkbox("Draw Test Dynamic Box Debug", &drawTestDynamicBoxDebug);
		ImGui::Checkbox("Draw Customer Interact Bounds", &drawCustomerInteractBounds);
	}

	ImGui::Separator();
	if (ImGui::CollapsingHeader("Cinematic Triggers"))
	{
		if (ImGui::Button("Add Cinematic Trigger"))
		{
			PushUndoSnapshot();

			CinematicTriggerZone trigger{};
			trigger.id = TextFormat(
				"cinematic_trigger_%i",
				(int)cinematicTriggers.size()
			);

			const Camera3D& placementCam = editMode ? editorCamera : camera;

			Vector3 forward = NormalizeSafe(
				Vector3Subtract(placementCam.target, placementCam.position)
			);

			trigger.position = Vector3Add(
				placementCam.position,
				Vector3Scale(forward, 3.0f)
			);

			trigger.position.y = placementCam.position.y;

			trigger.position.y = player.m_pos.y + 1.0f;

			trigger.lookTarget = Vector3Add(
				trigger.position,
				Vector3{ 0.0f, 1.0f, 3.0f }
			);

			trigger.size = { 2.0f, 2.0f, 2.0f };
			trigger.duration = 1.5f;
			trigger.enabled = true;
			trigger.repeatable = false;
			trigger.lockPlayerMovement = true;
			trigger.syncPlayerLookAtEnd = true;

			cinematicTriggers.push_back(trigger);
			selectedCinematicTriggerIndex = (int)cinematicTriggers.size() - 1;
		}

		ImGui::SameLine();

		if (ImGui::Button("Delete Selected Trigger"))
		{
			if (selectedCinematicTriggerIndex >= 0 &&
				selectedCinematicTriggerIndex < (int)cinematicTriggers.size())
			{
				PushUndoSnapshot();

				cinematicTriggers.erase(
					cinematicTriggers.begin() + selectedCinematicTriggerIndex
				);

				selectedCinematicTriggerIndex = -1;
			}
		}

		ImGui::Separator();

		for (int i = 0; i < (int)cinematicTriggers.size(); i++)
		{
			CinematicTriggerZone& trigger = cinematicTriggers[i];

			std::string label = TextFormat(
				"%s%s",
				i == selectedCinematicTriggerIndex ? "> " : "",
				trigger.id.c_str()
			);

			if (ImGui::Selectable(label.c_str(), selectedCinematicTriggerIndex == i))
			{
				selectedCinematicTriggerIndex = i;
			}
		}

		if (selectedCinematicTriggerIndex >= 0 &&
			selectedCinematicTriggerIndex < (int)cinematicTriggers.size())
		{
			ImGui::Separator();

			CinematicTriggerZone& trigger =
				cinematicTriggers[selectedCinematicTriggerIndex];

			char idBuffer[128] = {};
			strncpy_s(idBuffer, trigger.id.c_str(), sizeof(idBuffer) - 1);

			if (ImGui::InputText("Trigger ID", idBuffer, sizeof(idBuffer)))
			{
				trigger.id = idBuffer;
			}
			PushUndoIfItemActivated();

			ImGui::Checkbox("Enabled", &trigger.enabled);
			PushUndoIfItemActivated();

			ImGui::Checkbox("Repeatable", &trigger.repeatable);
			PushUndoIfItemActivated();

			ImGui::Checkbox("Lock Player Movement", &trigger.lockPlayerMovement);
			PushUndoIfItemActivated();

			ImGui::Checkbox("Sync Player Look At End", &trigger.syncPlayerLookAtEnd);
			PushUndoIfItemActivated();

			ImGui::DragFloat3("Position", &trigger.position.x, 0.05f);
			PushUndoIfItemActivated();

			ImGui::DragFloat3("Size", &trigger.size.x, 0.05f, 0.1f, 50.0f);
			PushUndoIfItemActivated();

			ImGui::DragFloat3("Look Target", &trigger.lookTarget.x, 0.05f);
			PushUndoIfItemActivated();

			ImGui::DragFloat("Duration", &trigger.duration, 0.05f, 0.05f, 20.0f);
			PushUndoIfItemActivated();

			ImGui::SeparatorText("Look Sound Trigger");

			ImGui::Checkbox("Enable Look Sound", &trigger.enableLookSound);
			PushUndoIfItemActivated();

			char lookSoundBuffer[128] = {};
			strncpy_s(
				lookSoundBuffer,
				trigger.lookSoundSfxId.c_str(),
				sizeof(lookSoundBuffer) - 1
			);

			if (ImGui::InputText("Look Sound SFX ID", lookSoundBuffer, sizeof(lookSoundBuffer)))
			{
				trigger.lookSoundSfxId = lookSoundBuffer;
			}
			PushUndoIfItemActivated();

			ImGui::DragFloat(
				"Look Sound Distance",
				&trigger.lookSoundDistance,
				0.05f,
				0.25f,
				30.0f
			);
			ImGui::DragFloat(
				"Look Sound Volume",
				&trigger.lookSoundVolume,
				0.01f,
				0.0f,
				1.0f
			);
			PushUndoIfItemActivated();

			ImGui::DragFloat(
				"Look Sound Fade In",
				&trigger.lookSoundFadeInSeconds,
				0.01f,
				0.01f,
				5.0f
			);
			PushUndoIfItemActivated();

			ImGui::DragFloat(
				"Look Sound Fade Out",
				&trigger.lookSoundFadeOutSeconds,
				0.01f,
				0.01f,
				5.0f
			);
			PushUndoIfItemActivated();

			PushUndoIfItemActivated();

			ImGui::Checkbox("Loop While Looking", &trigger.lookSoundLoop);
			PushUndoIfItemActivated();

			ImGui::TextDisabled("Example: SFX ID = cat_purr");
			ImGui::TextDisabled("Load it in LoadSoundEffects(): LoadSfx(\"cat_purr\", \"Audio/Soundfx/cat_purr.mp3\", 0.55f);");

			ImGui::Checkbox("Play Self Dialogue", &trigger.playSelfDialogue);
			PushUndoIfItemActivated();

			char voicePrefixBuffer[128] = {};
			strncpy_s(
				voicePrefixBuffer,
				trigger.selfDialogueVoicePrefix.c_str(),
				sizeof(voicePrefixBuffer) - 1
			);

			if (ImGui::InputText(
				"Voice Prefix",
				voicePrefixBuffer,
				sizeof(voicePrefixBuffer)
			))
			{
				trigger.selfDialogueVoicePrefix = voicePrefixBuffer;
			}
			PushUndoIfItemActivated();

			std::string joinedDialogue = JoinLines(trigger.selfDialogueLines);

			char dialogueBuffer[2048] = {};
			strncpy_s(
				dialogueBuffer,
				joinedDialogue.c_str(),
				sizeof(dialogueBuffer) - 1
			);

			if (ImGui::InputTextMultiline(
				"Self Dialogue Lines",
				dialogueBuffer,
				sizeof(dialogueBuffer),
				ImVec2(-1.0f, 120.0f)
			))
			{
				trigger.selfDialogueLines = SplitLines(dialogueBuffer);
			}
			PushUndoIfItemActivated();

			if (ImGui::Button("Set Trigger Position To Player"))
			{
				PushUndoSnapshot();
				trigger.position = {
					player.m_pos.x,
					player.m_pos.y + 1.0f,
					player.m_pos.z
				};
			}

			ImGui::SameLine();
			if (ImGui::Button("Set Trigger Position From Edit Camera"))
			{
				PushUndoSnapshot();

				const Camera3D& sourceCam = editMode ? editorCamera : camera;

				Vector3 forward = NormalizeSafe(
					Vector3Subtract(sourceCam.target, sourceCam.position)
				);

				trigger.position = Vector3Add(
					sourceCam.position,
					Vector3Scale(forward, 3.0f)
				);
			}

			ImGui::SameLine();

			if (ImGui::Button("Set Look Target From Camera"))
			{
				PushUndoSnapshot();

				const Camera3D& sourceCam = editMode ? editorCamera : camera;

				Vector3 forward = NormalizeSafe(
					Vector3Subtract(sourceCam.target, sourceCam.position)
				);

				trigger.lookTarget = Vector3Add(
					sourceCam.position,
					Vector3Scale(forward, 5.0f)
				);
			}

			if (ImGui::Button("Test Selected Trigger"))
			{
				StartCinematicLookAt(
					trigger.lookTarget,
					trigger.duration,
					trigger.lockPlayerMovement,
					trigger.syncPlayerLookAtEnd
				);

				if (trigger.playSelfDialogue && !trigger.selfDialogueLines.empty())
				{
					std::string scriptId = "__cine_test_" + SanitizeVoiceToken(trigger.id);

					std::string prefix = trigger.selfDialogueVoicePrefix.empty()
						? trigger.id
						: trigger.selfDialogueVoicePrefix;

					std::vector<SelfDialogueLine> lines;

					for (int i = 0; i < (int)trigger.selfDialogueLines.size(); i++)
					{
						SelfDialogueLine line{};
						line.text = trigger.selfDialogueLines[i];
						line.voicePath = MakeVoicePathFromPrefix(prefix, i);
						line.sfxId = "";
						lines.push_back(line);
					}

					StartSelfDialogueLines(lines, scriptId);
				}
			}

			ImGui::SameLine();

			if (ImGui::Button("Reset Runtime Trigger State"))
			{
				trigger.triggered = false;
				trigger.wasInside = false;
			}

			ImGui::Text(
				"Runtime: triggered=%i wasInside=%i",
				trigger.triggered ? 1 : 0,
				trigger.wasInside ? 1 : 0
			);
		}
	}

	ImGui::Separator();
	if (ImGui::CollapsingHeader("Bloom"))
	{
		ImGui::Checkbox("Enable Bloom", &bloomEnabled);
		ImGui::SliderFloat("Threshold", &bloomThreshold, 0.1f, 1.5f);
		ImGui::SliderFloat("Soft Knee", &bloomKnee, 0.01f, 0.5f);
		ImGui::SliderFloat("Intensity", &bloomIntensity, 0.0f, 2.0f);
		ImGui::SliderInt("Blur Passes", &bloomBlurPasses, 1, 8);
	}
	ImGui::Separator();

	if (ImGui::CollapsingHeader("Customer POIs"))
	{
		DrawCustomerPOIEditorUI();
	}

	if (ImGui::CollapsingHeader("Shadow Settings"))
	{
		DrawShadowSettingsUI();
	}

	if (ImGui::Button("Apply Default Scene Prop Shadow Rules"))
	{
		for (SceneProp& prop : sceneProps)
		{
			if (prop.model == nullptr)
			{
				prop.castsShadow = false;
				continue;
			}

			prop.castsShadow =
				ShouldScenePropCastShadowByName(prop.name) &&
				ShouldScenePropCastShadowByName(prop.sourceNodeName);
		}

		MarkShadowMapsDirty();
	}

	if (ImGui::CollapsingHeader("Outline Settings"))
	{
		DrawOutlineSettingsUI();
	}

	if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
	{
		DrawLightsEditorUI();
	}

	if (ImGui::CollapsingHeader("Legacy Flat Item List"))
	{
		DrawEditorItemList();

		ImGui::Separator();

		if (selectedEditorItemIndex >= 0 && selectedEditorItemIndex < (int)editorItems.size())
		{
			DrawEditorItemInspector(editorItems[selectedEditorItemIndex]);
		}
		else
		{
			ImGui::Text("No legacy item selected.");
		}
	}

	if (blockoutDirty)
	{
		ImGui::Separator();
		ImGui::TextColored(
			ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
			"Blockout changed. Physics is out of date."
		);

		if (ImGui::Button("Rebuild Physics World"))
		{
			UpdateScenePropWorldTransforms();
			RecreatePhysicsWorld();
		}
	}

	ImGui::End();

	DrawScenePropHierarchyPanel();
	DrawSelectedScenePropInspectorPanel();
}

void Game::BuildStoreBlockout()
{
	blockoutBoxes.clear();

	const float roomWidth = 12.0f;
	const float roomDepth = 8.0f;
	const float wallHeight = 3.2f;
	const float wallThickness = 0.2f;

	const float counterWidth = 2.4f;
	const float counterDepth = 0.8f;
	const float counterHeight = 1.0f;

	blockoutBoxes.push_back({
		"Front Wall",
		{ 0.0f, 1.6f, -4.0f },
		{ 12.0f, 3.2f, 0.3f },
		{ 0.0f, 0.0f, 0.0f },
		GRAY,

		true,   // hasCollision
		true,   // blocksPlayer
		false,   // blocksCustomers
		true,   // visible

		false,  // useNormalCollision
		true    // useJoltCollider
		});

	blockoutBoxes.push_back({
		"Back Wall",
		{ 0.0f, wallHeight * 0.5f, roomDepth * 0.5f },
		{ roomWidth, wallHeight, wallThickness },
		{ 0.0f, 0.0f, 0.0f },
		LIGHTGRAY,
		true, true, true,
		false, true
		});

	blockoutBoxes.push_back({
		"Left Wall",
		{ -roomWidth * 0.5f, wallHeight * 0.5f, 0.0f },
		{ wallThickness, wallHeight, roomDepth },
		{ 0.0f, 0.0f, 0.0f },
		LIGHTGRAY,
		true, true, true,
		false, true
		});

	blockoutBoxes.push_back({
		"Right Wall",
		{ roomWidth * 0.5f, wallHeight * 0.5f, 0.0f },
		{ wallThickness, wallHeight, roomDepth },
		{ 0.0f, 0.0f, 0.0f },
		LIGHTGRAY,
		true, true, true,
		false, true
		});

	blockoutBoxes.push_back({
		"Counter",
		{ 0.0f, counterHeight * 0.5f, -roomDepth * 0.5f + 1.4f },
		{ counterWidth, counterHeight, counterDepth },
		{ 0.0f, 0.0f, 0.0f },
		GRAY,
		true, true, true,
		false, true
		});
	blockoutBoxes.push_back({
	"Floor Collider",
	{ 0.0f, -0.1f, 0.0f },
	{ roomWidth, 0.2f, roomDepth },
	{ 0.0f, 0.0f, 0.0f },
	LIGHTGRAY,
	true,
	true,
	false,
	false,
	true
		});

	blockoutBoxes.push_back({
	"Player Only Barrier",
	{ 0.0f, 1.5f, 8.0f },
	{ 12.0f, 3.0f, 0.5f },
	{ 0.0f, 0.0f, 0.0f },
	Color{ 255, 0, 255, 80 },

	true,   // hasCollision
	true,   // blocksPlayer
	false,  // blocksCustomers
	false,  // visible

	false,  // useNormalCollision
	true    // useJoltCollider
		});


}
void Game::DrawPhysicsDebug() const
{
	// legacy/staticBodies
	for (const PhysicsBody& body : staticBodies)
	{
		if (!body.isActive) continue;

		Quaternion q = QuaternionFromEuler(
			body.rotationDeg.x * DEG2RAD,
			body.rotationDeg.y * DEG2RAD,
			body.rotationDeg.z * DEG2RAD
		);

		Vector3 axis;
		float angleDeg;
		QuaternionToAxisAngleSafe(q, axis, angleDeg);

		Vector3 size = {
			body.halfExtents.x * 2.0f,
			body.halfExtents.y * 2.0f,
			body.halfExtents.z * 2.0f
		};

		DrawModelWiresEx(blockoutCubeModel, body.position, axis, angleDeg, size, Fade(GREEN, 0.2f));
	}

	// scene prop Jolt colliders
	for (const SceneProp& prop : sceneProps)
	{
		if (prop.bodyId.IsInvalid() || !prop.hasCollision) continue;

		Vector3 center = physics->GetBodyPosition(prop.bodyId);
		Quaternion q = physics->GetBodyRotation(prop.bodyId);

		Vector3 axis;
		float angleDeg;
		QuaternionToAxisAngleSafe(q, axis, angleDeg);

		Vector3 size = {
			prop.colliderSize.x * prop.scale.x,
			prop.colliderSize.y * prop.scale.y,
			prop.colliderSize.z * prop.scale.z
		};

		DrawModelWiresEx(blockoutCubeModel, center, axis, angleDeg, size, Fade(ORANGE, 0.35f));
	}

	// test dynamic box
	/*
		if (hasTestDynamicBox)
	{
		Vector3 center = physics->GetBodyPosition(testDynamicBox);
		Quaternion q = physics->GetBodyRotation(testDynamicBox);

		Vector3 axis;
		float angleDeg;
		QuaternionToAxisAngleSafe(q, axis, angleDeg);

		DrawModelWiresEx(
			blockoutCubeModel,
			center,
			axis,
			angleDeg,
			{ 0.5f, 0.5f, 0.5f },
			Fade(RED, 0.35f)
		);
	}
	*/

	DrawScenePropColliderDebug();

}
void Game::BuildStaticBodiesFromBlockout()
{
	staticBodies.clear();

	for (const BlockoutBox& box : blockoutBoxes)
	{
		if (!box.hasCollision) continue;

		bool rotated =
			fabsf(box.rotationDeg.x) > 0.01f ||
			fabsf(box.rotationDeg.y) > 0.01f ||
			fabsf(box.rotationDeg.z) > 0.01f;

		PhysicsBody body;
		body.type = BodyType::Static;
		body.position = box.position;
		body.rotationDeg = box.rotationDeg;
		body.blocksPlayer = box.blocksPlayer;
		body.velocity = { 0.0f, 0.0f, 0.0f };
		body.halfExtents = {
			box.size.x * 0.5f,
			box.size.y * 0.5f,
			box.size.z * 0.5f
		};
		body.invMass = 0.0f;
		body.useGravity = false;
		body.isTrigger = false;
		body.isActive = true;

		// Legacy AABB collision only works for non-rotated objects
		body.useJoltCollider = box.useJoltCollider || rotated;

		if (box.useNormalCollision && !rotated)
		{
			staticBodies.emplace_back(body);
		}

		if (body.useJoltCollider)
		{
			Quaternion rot = QuaternionFromEuler(
				box.rotationDeg.x * DEG2RAD,
				box.rotationDeg.y * DEG2RAD,
				box.rotationDeg.z * DEG2RAD
			);

			physics->AddStaticBox(box.position, body.halfExtents, rot);
		}
	}
}
void Game::DrawStoreBlockout() const
{
	Vector2 oneTiling = { 1.0f, 1.0f };
	Vector2 zeroOffset = { 0.0f, 0.0f };

	float metallic = blockoutCubeModel.materials[0].maps[MATERIAL_MAP_METALNESS].value;
	float roughness = blockoutCubeModel.materials[0].maps[MATERIAL_MAP_ROUGHNESS].value;
	float ao = blockoutCubeModel.materials[0].maps[MATERIAL_MAP_OCCLUSION].value;
	float normalStrength = 1.0f;
	float emissiveIntensity = 0.0f;

	Vector4 albedoColor = ColorNormalize(blockoutCubeModel.materials[0].maps[MATERIAL_MAP_ALBEDO].color);
	Vector4 emissiveColor = ColorNormalize(blockoutCubeModel.materials[0].maps[MATERIAL_MAP_EMISSION].color);

	SetShaderValue(pbrShader, textureTilingLoc, &oneTiling, SHADER_UNIFORM_VEC2);
	SetShaderValue(pbrShader, offsetLoc, &zeroOffset, SHADER_UNIFORM_VEC2);

	SetShaderValue(pbrShader, metallicValueLoc, &metallic, SHADER_UNIFORM_FLOAT);
	SetShaderValue(pbrShader, roughnessValueLoc, &roughness, SHADER_UNIFORM_FLOAT);
	SetShaderValue(pbrShader, aoValueLoc, &ao, SHADER_UNIFORM_FLOAT);
	SetShaderValue(pbrShader, normalValueLoc, &normalStrength, SHADER_UNIFORM_FLOAT);
	SetShaderValue(pbrShader, emissiveIntensityLoc, &emissiveIntensity, SHADER_UNIFORM_FLOAT);

	SetShaderValue(pbrShader, albedoColorLoc, &albedoColor, SHADER_UNIFORM_VEC4);
	SetShaderValue(pbrShader, emissiveColorLoc, &emissiveColor, SHADER_UNIFORM_VEC4);

	SetTextureUsage(pbrShader, 0, 0, 0, 0);

	for (const BlockoutBox& box : blockoutBoxes)
	{
		if (!box.visible) continue;

		Quaternion q = QuaternionFromEuler(
			box.rotationDeg.x * DEG2RAD,
			box.rotationDeg.y * DEG2RAD,
			box.rotationDeg.z * DEG2RAD
		);

		Vector3 axis;
		float angleDeg;
		QuaternionToAxisAngleSafe(q, axis, angleDeg);

		DrawModelEx(blockoutCubeModel, box.position, axis, angleDeg, box.size, box.color);
	}
}

BoundingBox Game::BoxToBounds(const BlockoutBox& box) const
{
	Vector3 half = {
		box.size.x * 0.5f,
		box.size.y * 0.5f,
		box.size.z * 0.5f
	};

	BoundingBox bounds = {};
	bounds.min = Vector3Subtract(box.position, half);
	bounds.max = Vector3Add(box.position, half);
	return bounds;
}

AABB Game::MakeAABB(const Vector3& position, const Vector3& halfExtents) const
{
	AABB box;
	box.min = {
		position.x - halfExtents.x,
		position.y - halfExtents.y,
		position.z - halfExtents.z
	};
	box.max = {
		position.x + halfExtents.x,
		position.y + halfExtents.y,
		position.z + halfExtents.z
	};
	return box;
}

AABB Game::MakeBlockoutAABB(const BlockoutBox& box) const
{
	Vector3 half = {
		box.size.x * 0.5f,
		box.size.y * 0.5f,
		box.size.z * 0.5f
	};

	return MakeAABB(box.position, half);
}
BoundingBox Game::GetPlayerBoundsAt(const Vector3& pos) const
{
	const float playerRadius = 0.35f;
	const float playerHeight = 1.8f;

	BoundingBox box = {};
	box.min = {
		pos.x - playerRadius,
		pos.y,
		pos.z - playerRadius
	};
	box.max = {
		pos.x + playerRadius,
		pos.y + playerHeight,
		pos.z + playerRadius
	};
	return box;
}

BoundingBox Game::BodyToBounds(const PhysicsBody& body) const
{
	BoundingBox box = {};
	box.min = {
		body.position.x - body.halfExtents.x,
		body.position.y - body.halfExtents.y,
		body.position.z - body.halfExtents.z
	};
	box.max = {
		body.position.x + body.halfExtents.x,
		body.position.y + body.halfExtents.y,
		body.position.z + body.halfExtents.z
	};
	return box;
}

bool Game::Intersects(const BoundingBox& a, const BoundingBox& b) const
{
	return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
		(a.min.y <= b.max.y && a.max.y >= b.min.y) &&
		(a.min.z <= b.max.z && a.max.z >= b.min.z);
}
void Game::ResolvePlayerCollisions()
{
	BoundingBox playerBox = GetPlayerBoundsAt(player.m_pos);


	for (const PhysicsBody& body : staticBodies)
	{

		if (!body.isActive) continue;

		if (fabsf(body.rotationDeg.x) > 0.01f ||
			fabsf(body.rotationDeg.y) > 0.01f ||
			fabsf(body.rotationDeg.z) > 0.01f)
		{
			continue;
		}

		if (body.halfExtents.y <= 0.15f)
			continue;

		BoundingBox wallBox = BodyToBounds(body);

		if (!Intersects(playerBox, wallBox)) continue;

		float overlapLeft = playerBox.max.x - wallBox.min.x;
		float overlapRight = wallBox.max.x - playerBox.min.x;
		float overlapFront = playerBox.max.z - wallBox.min.z;
		float overlapBack = wallBox.max.z - playerBox.min.z;

		float resolveX = (overlapLeft < overlapRight) ? -overlapLeft : overlapRight;
		float resolveZ = (overlapFront < overlapBack) ? -overlapFront : overlapBack;

		if (fabsf(resolveX) < fabsf(resolveZ))
		{
			player.m_pos.x += resolveX;
		}
		else
		{
			player.m_pos.z += resolveZ;
		}

		playerBox = GetPlayerBoundsAt(player.m_pos);
	}
}

Vector3 Game::GetCameraForward() const
{
	return NormalizeSafe(Vector3Subtract(camera.target, camera.position));
}

void Game::ToggleCursorMode()
{
	cursorUnlocked = !cursorUnlocked;

	if (cursorUnlocked)
	{
		EnableCursor();
		cursorCapturedByGame = false;
	}
	else
	{
		SetMousePosition(GetScreenWidth() / 2, GetScreenHeight() / 2);
		DisableCursor();
		cursorCapturedByGame = true;
	}
}

void Game::HandleBoxInteraction()
{
	if (!IsKeyPressed(KEY_E))
		return;

	// Do not allow drop/pickup while scan animation is running.
	if (heldItemScanState != HeldItemScanState::None)
		return;

	if (!hasHeldBody)
	{
		InteractHit hit = FindInteractableBodyRaycast();

		if (hit.valid && hit.scenePropIndex >= 0)
		{
			const SceneProp& prop = sceneProps[hit.scenePropIndex];

			// Store buttons are interactable, but not pickup items.
			if (IsStoreControlProp(prop))
				return;

			StartHoldingBody(hit.bodyId);
		}

		return;
	}

	if (heldScenePropIndex >= 0 &&
		heldScenePropIndex < (int)sceneProps.size())
	{
		// First priority:
		// If this is already a scanned buyer item, E near the counter should
		// return it to the customer, not try to scan/drop it again.
		if (TryReturnHeldScannedCustomerItemToCounter())
		{
			return;
		}

		// Second priority:
		// Start scanner animation only for unscanned items.
		if (TryStartHeldItemScanFromCurrentTarget(heldScenePropIndex))
		{
			return;
		}
	}
	StopHoldingBody();
}
void Game::ClearHeldPropTarget()
{
	if (heldPropTarget.id == 0)
		return;

	BeginTextureMode(heldPropTarget);
	ClearBackground(BLANK);
	EndTextureMode();
}
//edit mode
void Game::SyncEditorCameraFromCurrentCamera()
{
	editorCamera = camera;
	editorCamPos = camera.position;

	Vector3 forward = NormalizeSafe(Vector3Subtract(camera.target, camera.position));

	editorPitch = asinf(forward.y);
	editorYaw = atan2f(forward.x, forward.z);
}

void Game::EnterEditMode()
{
#if !GAME_ENABLE_EDITOR
	return;
#endif

	editMode = true;
	SyncEditorCameraFromCurrentCamera();
	EnableCursor();
	cursorUnlocked = true;
	editCamNavigating = false;
}

void Game::ExitEditMode()
{
	editMode = false;
	editCamNavigating = false;
	DisableCursor();
	cursorUnlocked = false;
}

void Game::UpdateEditCamera(float dt)
{
	ImGuiIO& io = ImGui::GetIO();

	bool wantsKeyboard = io.WantCaptureKeyboard;
	bool wantsMouse = io.WantCaptureMouse;

	bool rmbDown = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);

	if (rmbDown && !wantsMouse)
	{
		if (!editCamNavigating)
		{
			DisableCursor();
			editCamNavigating = true;
		}

		Vector2 md = GetMouseDelta();

		editorYaw -= md.x * editorLookSpeed;
		editorPitch -= md.y * editorLookSpeed;
		editorPitch = Clamp(editorPitch, -1.55f, 1.55f);
	}
	else
	{
		if (editCamNavigating)
		{
			EnableCursor();
			editCamNavigating = false;
		}
	}

	Vector3 forward = {
		cosf(editorPitch) * sinf(editorYaw),
		sinf(editorPitch),
		cosf(editorPitch) * cosf(editorYaw)
	};
	forward = NormalizeSafe(forward);

	Vector3 right = NormalizeSafe(Vector3CrossProduct(forward, { 0.0f, 1.0f, 0.0f }));
	Vector3 up = { 0.0f, 1.0f, 0.0f };

	if (editCamNavigating && !wantsKeyboard)
	{
		float speed = editorMoveSpeed * dt;
		if (IsKeyDown(KEY_LEFT_SHIFT)) speed *= 3.0f;

		if (IsKeyDown(KEY_W)) editorCamPos = Vector3Add(editorCamPos, Vector3Scale(forward, speed));
		if (IsKeyDown(KEY_S)) editorCamPos = Vector3Subtract(editorCamPos, Vector3Scale(forward, speed));
		if (IsKeyDown(KEY_A)) editorCamPos = Vector3Subtract(editorCamPos, Vector3Scale(right, speed));
		if (IsKeyDown(KEY_D)) editorCamPos = Vector3Add(editorCamPos, Vector3Scale(right, speed));
		if (IsKeyDown(KEY_Q)) editorCamPos = Vector3Subtract(editorCamPos, Vector3Scale(up, speed));
		if (IsKeyDown(KEY_E)) editorCamPos = Vector3Add(editorCamPos, Vector3Scale(up, speed));
	}

	editorCamera.position = editorCamPos;
	editorCamera.target = Vector3Add(editorCamPos, forward);
	editorCamera.up = { 0.0f, 1.0f, 0.0f };
	editorCamera.fovy = 60.0f;
	editorCamera.projection = CAMERA_PERSPECTIVE;
}

bool Game::SaveSceneState()
{
	CaptureLiveScenePropPhysicsTransforms();
	UpdateScenePropWorldTransforms();
	ApplyImportedEditorTransformsToRuntime();

	auto IsBadScale = [](Vector3 s)
		{
			return !std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.z) ||
				fabsf(s.x) < 0.0001f ||
				fabsf(s.y) < 0.0001f ||
				fabsf(s.z) < 0.0001f;
		};

	std::ofstream out("Data/scene_state.txt");
	if (!out.is_open()) return false;

	out << "NAMES "
		<< std::quoted(floorName) << ' '
		<< std::quoted(propName) << ' '
		<< std::quoted(physicsBoxName) << '\n';

	out << "FLOOR "
		<< floorDrawPos.x << ' ' << floorDrawPos.y << ' ' << floorDrawPos.z << ' '
		<< floorDrawRotDeg.x << ' ' << floorDrawRotDeg.y << ' ' << floorDrawRotDeg.z << '\n';

	out << "PROP "
		<< propDrawPos.x << ' ' << propDrawPos.y << ' ' << propDrawPos.z << ' '
		<< propDrawRotDeg.x << ' ' << propDrawRotDeg.y << ' ' << propDrawRotDeg.z << '\n';

	out << "LIGHTS_V2 " << lightCount << '\n';

	for (int i = 0; i < lightCount; i++)
	{
		const Light& l = lights[i];

		out << std::quoted(l.name) << ' '
			<< l.type << ' '

			<< l.position.x << ' '
			<< l.position.y << ' '
			<< l.position.z << ' '

			<< l.target.x << ' '
			<< l.target.y << ' '
			<< l.target.z << ' '

			<< l.intensity << ' '
			<< l.enabled << ' '

			<< l.color[0] << ' '
			<< l.color[1] << ' '
			<< l.color[2] << ' '
			<< l.color[3] << ' '

			<< l.range << ' '
			<< l.castsShadow << ' '
			<< l.shadowRange << ' '
			<< l.shadowBias << ' '
			<< l.shadowStrength << ' '
			<< l.shadowOrthoSize << ' '
			<< l.shadowNear << ' '
			<< l.shadowFar << ' '
			<< l.shadowFovy << '\n';
	}

	out << "BLOCKOUT " << blockoutBoxes.size() << '\n';
	for (const BlockoutBox& box : blockoutBoxes)
	{
		out << std::quoted(box.name) << ' '
			<< box.position.x << ' ' << box.position.y << ' ' << box.position.z << ' '
			<< box.size.x << ' ' << box.size.y << ' ' << box.size.z << ' '
			<< box.rotationDeg.x << ' ' << box.rotationDeg.y << ' ' << box.rotationDeg.z << ' '
			<< box.hasCollision << ' '
			<< box.blocksPlayer << ' '
			<< box.visible << ' '
			<< box.useNormalCollision << ' '
			<< box.useJoltCollider << '\n';
	}
	/*
		out << "SCENEPROPS " << sceneProps.size() << '\n';
	for (const SceneProp& p : sceneProps)
	{
		out << std::quoted(p.name) << ' '
			<< p.position.x << ' ' << p.position.y << ' ' << p.position.z << ' '
			<< p.rotationDeg.x << ' ' << p.rotationDeg.y << ' ' << p.rotationDeg.z << ' '
			<< p.scale.x << ' ' << p.scale.y << ' ' << p.scale.z << ' '
			<< p.lockUniformScale << ' '
			<< p.visible << ' '
			<< p.hasCollision << ' '
			<< p.blocksPlayer << ' '
			<< p.useNormalCollision << ' '
			<< p.useJoltCollider << ' '
			<< p.simulatePhysics << ' '
			<< p.syncFromPhysics << ' '
			<< p.editLockPhysics << ' '
			<< p.colliderOffset.x << ' ' << p.colliderOffset.y << ' ' << p.colliderOffset.z << ' '
			<< p.colliderSize.x << ' ' << p.colliderSize.y << ' ' << p.colliderSize.z << '\n';
	}
	*/


	out << "SHADOWPERF\n";
	out << shadowMapsUpdateEveryFrame << ' '
		<< shadowMapsUpdateWhenDynamicPropsExist << ' '
		<< shadowMapsUpdateForCustomers << '\n';

	out << "BLOOM\n";
	out << bloomEnabled << ' '
		<< bloomThreshold << ' '
		<< bloomKnee << ' '
		<< bloomIntensity << ' '
		<< bloomBlurPasses << '\n';

	out << "OUTLINE\n";

	out << (int)pickupOutlineColor.r << ' '
		<< (int)pickupOutlineColor.g << ' '
		<< (int)pickupOutlineColor.b << ' '
		<< (int)pickupOutlineColor.a << '\n';

	out << pickupOutlineWidth << '\n';

	out << pickupOutlinePulseEnabled << '\n';
	out << pickupOutlinePulseMinWidth << '\n';
	out << pickupOutlinePulseMaxWidth << '\n';
	out << pickupOutlinePulseSpeed << '\n';
	out << pickupOutlinePulseMinAlpha << '\n';
	out << pickupOutlinePulseMaxAlpha << '\n';

	out << pickupOccludedOutlineEnabled << '\n';
	out << pickupOccludedOutlineWidth << '\n';

	out << (int)pickupOccludedOutlineColor.r << ' '
		<< (int)pickupOccludedOutlineColor.g << ' '
		<< (int)pickupOccludedOutlineColor.b << ' '
		<< (int)pickupOccludedOutlineColor.a << '\n';

	out << pickupHiddenFillEnabled << '\n';
	out << pickupHiddenFillWidth << '\n';

	out << (int)pickupHiddenFillColor.r << ' '
		<< (int)pickupHiddenFillColor.g << ' '
		<< (int)pickupHiddenFillColor.b << ' '
		<< (int)pickupHiddenFillColor.a << '\n';

	if (hasTestDynamicBox)
	{
		Vector3 p = physics->GetBodyPosition(testDynamicBox);
		Quaternion q = physics->GetBodyRotation(testDynamicBox);

		out << "PHYSBOX "
			<< p.x << ' ' << p.y << ' ' << p.z << ' '
			<< q.x << ' ' << q.y << ' ' << q.z << ' ' << q.w << '\n';
	}


	auto IsRuntimeCustomerSceneProp = [&](const SceneProp& p) -> bool
		{
			bool nameLooksRuntimeCustomerItem =
				p.name.rfind("Customer Item - ", 0) == 0;

			if (p.placedByCustomer)
				return true;

			if (p.owningCustomerIndex >= 0)
				return true;

			// Important:
			// Only treat name-based customer items as runtime if they are NOT imported GLB props.
			// If an imported prop was corrupted and renamed "Customer Item - ...",
			// RestoreImportedNames should fix it, not this save filter.
			if (!p.importedFromGlbScene && nameLooksRuntimeCustomerItem)
				return true;

			return false;
		};

	std::vector<int> persistentScenePropIndices;
	persistentScenePropIndices.reserve(sceneProps.size());

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		const SceneProp& p = sceneProps[i];

		if (IsRuntimeCustomerSceneProp(p))
			continue;

		persistentScenePropIndices.push_back(i);
	}

	out << "SCENEPROPS_V2 " << persistentScenePropIndices.size() << "\n";

	for (int savedIndex : persistentScenePropIndices)
	{
		const SceneProp& p = sceneProps[savedIndex];

		if (IsBadScale(p.scale))
		{
			TraceLog(
				LOG_WARNING,
				"Saving scene prop with bad scale: index=%i name=%s scale=(%.4f %.4f %.4f) local=(%.4f %.4f %.4f) editor=(%.4f %.4f %.4f)",
				savedIndex,
				p.name.c_str(),
				p.scale.x, p.scale.y, p.scale.z,
				p.localScale.x, p.localScale.y, p.localScale.z,
				p.importedEditorScale.x, p.importedEditorScale.y, p.importedEditorScale.z
			);
		}

		out
			<< std::quoted(GetScenePropStateKey(p, savedIndex)) << " "
			<< std::quoted(p.name) << " "
			<< std::quoted(p.sourceGlbPath) << " "
			<< std::quoted(p.sourceNodeName) << " "
			<< p.sourceNodeIndex << " "
			<< p.importedFromGlbScene << " "
			<< p.parentIndex << " "

			<< p.position.x << " " << p.position.y << " " << p.position.z << " "
			<< p.rotationDeg.x << " " << p.rotationDeg.y << " " << p.rotationDeg.z << " "
			<< p.scale.x << " " << p.scale.y << " " << p.scale.z << " "

			<< p.localPosition.x << " " << p.localPosition.y << " " << p.localPosition.z << " "
			<< p.localRotationDeg.x << " " << p.localRotationDeg.y << " " << p.localRotationDeg.z << " "
			<< p.localScale.x << " " << p.localScale.y << " " << p.localScale.z << " "

			<< p.visible << " "
			<< p.lockUniformScale << " "
			<< p.hasCollision << " "
			<< p.blocksPlayer << " "
			<< p.useNormalCollision << " "
			<< p.useJoltCollider << " "
			<< p.simulatePhysics << " "
			<< p.syncFromPhysics << " "
			<< p.editLockPhysics << " "

			<< p.colliderOffset.x << " " << p.colliderOffset.y << " " << p.colliderOffset.z << " "
			<< p.colliderSize.x << " " << p.colliderSize.y << " " << p.colliderSize.z
			<< "\n";
	}

	int importedOffsetCount = 0;

	for (const SceneProp& p : sceneProps)
	{
		if (p.importedFromGlbScene && p.model != nullptr)
			importedOffsetCount++;
	}

	out << "SCENEPROP_COLLIDER_OVERRIDES " << persistentScenePropIndices.size() << "\n";

	for (int savedIndex : persistentScenePropIndices)
	{
		const SceneProp& p = sceneProps[savedIndex];

		out
			<< std::quoted(GetScenePropStateKey(p, savedIndex)) << " "
			<< p.manualColliderOverride << " "
			<< p.colliderOffset.x << " "
			<< p.colliderOffset.y << " "
			<< p.colliderOffset.z << " "
			<< p.colliderSize.x << " "
			<< p.colliderSize.y << " "
			<< p.colliderSize.z << " "
			<< p.hasCollision << " "
			<< p.blocksPlayer << " "
			<< p.useJoltCollider << " "
			<< p.useNormalCollision
			<< "\n";
	}

	out << "SCENEPROP_SHADOWS " << persistentScenePropIndices.size() << "\n";

	for (int savedIndex : persistentScenePropIndices)
	{
		const SceneProp& p = sceneProps[savedIndex];

		out
			<< std::quoted(GetScenePropStateKey(p, savedIndex)) << " "
			<< p.castsShadow
			<< "\n";
	}

	std::vector<int> importedOffsetIndices;

	for (int savedIndex : persistentScenePropIndices)
	{
		const SceneProp& p = sceneProps[savedIndex];

		if (p.importedFromGlbScene && p.model != nullptr)
		{
			importedOffsetIndices.push_back(savedIndex);
		}
	}

	out << "IMPORTED_PROP_OFFSETS " << importedOffsetIndices.size() << "\n";

	for (int savedIndex : importedOffsetIndices)
	{
		const SceneProp& p = sceneProps[savedIndex];

		out
			<< std::quoted(GetScenePropStateKey(p, savedIndex)) << " "
			<< p.importedEditorOffset.x << " "
			<< p.importedEditorOffset.y << " "
			<< p.importedEditorOffset.z << " "
			<< p.importedEditorRotationDeg.x << " "
			<< p.importedEditorRotationDeg.y << " "
			<< p.importedEditorRotationDeg.z << " "
			<< p.importedEditorScale.x << " "
			<< p.importedEditorScale.y << " "
			<< p.importedEditorScale.z
			<< "\n";
	}

	int pickupHoldCount = 0;

	for (const SceneProp& p : sceneProps)
	{
		if (p.canPickup)
			pickupHoldCount++;
	}

	out << "SCENEPROP_OCCLUSION " << persistentScenePropIndices.size() << "\n";

	for (int savedIndex : persistentScenePropIndices)
	{
		const SceneProp& p = sceneProps[savedIndex];

		out
			<< std::quoted(GetScenePropStateKey(p, savedIndex)) << " "
			<< p.canOcclude << " "
			<< p.ignoreOcclusionCulling
			<< "\n";
	}


	std::vector<int> pickupHoldIndices;

	for (int savedIndex : persistentScenePropIndices)
	{
		const SceneProp& p = sceneProps[savedIndex];

		if (p.canPickup)
			pickupHoldIndices.push_back(savedIndex);
	}

	out << "PICKUP_HOLD " << pickupHoldIndices.size() << "\n";

	for (int savedIndex : pickupHoldIndices)
	{
		const SceneProp& p = sceneProps[savedIndex];

		out
			<< std::quoted(GetScenePropStateKey(p, savedIndex)) << " "
			<< p.canPickup << " "

			<< p.holdOffsetLocal.x << " "
			<< p.holdOffsetLocal.y << " "
			<< p.holdOffsetLocal.z << " "

			<< p.holdRotationOffsetDeg.x << " "
			<< p.holdRotationOffsetDeg.y << " "
			<< p.holdRotationOffsetDeg.z << " "

			<< p.holdFollowCameraPitch << " "
			<< p.snapUprightOnDrop << " "

			<< p.dropRotationOffsetDeg.x << " "
			<< p.dropRotationOffsetDeg.y << " "
			<< p.dropRotationOffsetDeg.z
			<< "\n";
	}

	std::vector<int> tradeTagIndices;

	for (int savedIndex : persistentScenePropIndices)
	{
		const SceneProp& p = sceneProps[savedIndex];

		if (p.pickupCategory != PickupItemCategory::Generic ||
			!p.tradeItemId.empty() ||
			!p.itemTag.empty())
		{
			tradeTagIndices.push_back(savedIndex);
		}
	}

	out << "SCENEPROP_TRADE_TAGS " << tradeTagIndices.size() << "\n";

	for (int savedIndex : tradeTagIndices)
	{
		const SceneProp& p = sceneProps[savedIndex];

		out
			<< std::quoted(GetScenePropStateKey(p, savedIndex)) << " "
			<< (int)p.pickupCategory << " "
			<< std::quoted(p.tradeItemId) << " "
			<< std::quoted(p.itemTag)
			<< "\n";
	}

	int inspectDialogueCount = 0;

	for (const SceneProp& p : sceneProps)
	{
		if (!p.inspectDialogueTag.empty() || !p.inspectDialogueLines.empty())
			inspectDialogueCount++;
	}

	out << "SCENEPROP_INSPECT_DIALOGUE " << inspectDialogueCount << "\n";

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		const SceneProp& p = sceneProps[i];

		if (p.inspectDialogueTag.empty() && p.inspectDialogueLines.empty())
			continue;

		out
			<< std::quoted(GetScenePropStateKey(p, i)) << " "
			<< std::quoted(p.inspectDialogueTag) << " "
			<< p.inspectDialogueLines.size();

		for (const std::string& line : p.inspectDialogueLines)
		{
			out << " " << std::quoted(line);
		}

		out << "\n";
	}

	out << "ITEM_PLACEMENT_SPOTS " << itemPlacementSpots.size() << "\n";

	for (const ItemPlacementSpot& spot : itemPlacementSpots)
	{
		out
			<< std::quoted(spot.id) << " "
			<< (int)spot.kind << " "

			<< spot.position.x << " "
			<< spot.position.y << " "
			<< spot.position.z << " "

			<< spot.rotationDeg.x << " "
			<< spot.rotationDeg.y << " "
			<< spot.rotationDeg.z << " "

			<< spot.scale.x << " "
			<< spot.scale.y << " "
			<< spot.scale.z << " "

			<< spot.snapRadius << " "
			<< spot.enabled << " "
			<< spot.allowPlayerDrop << " "
			<< spot.allowCustomerPlace << " "
			<< std::quoted(spot.acceptedItemTag)
			<< "\n";
	}

	std::vector<int> placementIndices;

	for (int savedIndex : persistentScenePropIndices)
	{
		const SceneProp& p = sceneProps[savedIndex];

		if (!p.itemTag.empty() ||
			p.currentPlacementSpotIndex >= 0 ||
			p.homePlacementSpotIndex >= 0 ||
			!p.preferHomePlacement ||
			p.placedByCustomer)
		{
			placementIndices.push_back(savedIndex);
		}
	}

	out << "SCENEPROP_PLACEMENT " << placementIndices.size() << "\n";

	for (int savedIndex : placementIndices)
	{
		const SceneProp& p = sceneProps[savedIndex];

		std::string currentSpotId = "";
		std::string homeSpotId = "";

		if (p.currentPlacementSpotIndex >= 0 &&
			p.currentPlacementSpotIndex < (int)itemPlacementSpots.size())
		{
			currentSpotId = itemPlacementSpots[p.currentPlacementSpotIndex].id;
		}

		if (p.homePlacementSpotIndex >= 0 &&
			p.homePlacementSpotIndex < (int)itemPlacementSpots.size())
		{
			homeSpotId = itemPlacementSpots[p.homePlacementSpotIndex].id;
		}

		out
			<< std::quoted(GetScenePropStateKey(p, savedIndex)) << " "
			<< std::quoted(p.itemTag) << " "
			<< std::quoted(currentSpotId) << " "
			<< std::quoted(homeSpotId) << " "
			<< p.preferHomePlacement << " "
			<< p.placedByCustomer
			<< "\n";
	}

	out << "INSTANCEDPROPS " << instancedProps.size() << '\n';

	for (const InstancedProp& p : instancedProps)
	{
		out << std::quoted(p.name) << ' '
			<< (int)p.type << ' '
			<< p.position.x << ' ' << p.position.y << ' ' << p.position.z << ' '
			<< p.rotationDeg.x << ' ' << p.rotationDeg.y << ' ' << p.rotationDeg.z << ' '
			<< p.scale.x << ' ' << p.scale.y << ' ' << p.scale.z << ' '
			<< p.visible << '\n';
	}

	out << "CUSTOMER_POIS_V3 " << customerPOIs.size() << "\n";

	for (const CustomerPOI& poi : customerPOIs)
	{
		out
			<< std::quoted(poi.id) << " "
			<< std::quoted(poi.group) << " "
			<< poi.position.x << " "
			<< poi.position.y << " "
			<< poi.position.z << " "
			<< poi.radius << " "
			<< poi.waitSecondsMin << " "
			<< poi.waitSecondsMax << " "
			<< poi.enabled << " "
			<< poi.stopPoint << " "
			<< poi.exclusive << " "
			<< poi.capacity << " "
			<< (int)poi.kind << " "
			<< poi.queueOrder << " "
			<< poi.useFacingDirection << " "
			<< poi.facingYawDeg
			<< "\n";
	}
	out << "CUSTOMERS " << customers.size() << "\n";

	for (int i = 0; i < (int)customers.size(); i++)
	{
		const Customer& c = customers[i];

		out
			<< i << " "
			<< c.position.x << " " << c.position.y << " " << c.position.z << " "
			<< c.targetPosition.x << " " << c.targetPosition.y << " " << c.targetPosition.z << " "
			<< c.yawDeg << " "
			<< c.scale.x << " " << c.scale.y << " " << c.scale.z << " "
			<< c.moveSpeed << " "
			<< c.usePOINavigation << " "
			<< c.editorFrozen << " "
			<< c.currentPOIIndex << " "
			<< c.targetPOIIndex << " "
			<< c.destinationPOIIndex << " "
			<< std::quoted(c.dialogueScriptId) << " "
			<< std::quoted(c.poiGroup)
			<< "\n";
	}

	out << "CINEMATIC_TRIGGERS_V4 " << cinematicTriggers.size() << "\n";

	for (const CinematicTriggerZone& trigger : cinematicTriggers)
	{
		out
			<< std::quoted(trigger.id) << " "

			<< trigger.position.x << " "
			<< trigger.position.y << " "
			<< trigger.position.z << " "

			<< trigger.size.x << " "
			<< trigger.size.y << " "
			<< trigger.size.z << " "

			<< trigger.lookTarget.x << " "
			<< trigger.lookTarget.y << " "
			<< trigger.lookTarget.z << " "

			<< trigger.duration << " "

			<< trigger.enabled << " "
			<< trigger.repeatable << " "
			<< trigger.lockPlayerMovement << " "
			<< trigger.syncPlayerLookAtEnd << " "

			<< trigger.enableLookSound << " "
			<< std::quoted(trigger.lookSoundSfxId) << " "
			<< trigger.lookSoundDistance << " "
			<< trigger.lookSoundLoop << " "
			<< trigger.lookSoundVolume << " "
			<< trigger.lookSoundFadeInSeconds << " "
			<< trigger.lookSoundFadeOutSeconds << " "

			<< " "
			<< trigger.playSelfDialogue << " "
			<< std::quoted(trigger.selfDialogueVoicePrefix) << " "
			<< std::quoted(JoinLines(trigger.selfDialogueLines))

			<< "\n";
	}

	out << "DIALOGUE_SETTINGS_V1 "
		<< customerDialogueAutoPlayEnabled
		<< "\n";

	return true;
}

bool Game::LoadSceneState()
{
	std::ifstream in("Data/scene_state.txt");
	if (!in.is_open()) return false;

	std::string tag;

	in >> tag;
	if (tag == "NAMES")
	{
		in >> std::quoted(floorName)
			>> std::quoted(propName)
			>> std::quoted(physicsBoxName);

		in >> tag;
	}

	if (tag == "FLOOR")
	{
		in >> floorDrawPos.x >> floorDrawPos.y >> floorDrawPos.z
			>> floorDrawRotDeg.x >> floorDrawRotDeg.y >> floorDrawRotDeg.z;
	}

	in >> tag;
	if (tag == "PROP")
	{
		in >> propDrawPos.x >> propDrawPos.y >> propDrawPos.z
			>> propDrawRotDeg.x >> propDrawRotDeg.y >> propDrawRotDeg.z;
	}
	int savedLights = 0;
	in >> tag >> savedLights;

	auto ToInt = [](float v) -> int
		{
			return (int)roundf(v);
		};

	auto ToBoolInt = [](float v) -> int
		{
			return fabsf(v) > 0.5f ? 1 : 0;
		};

	auto ClampLoadedLight = [](Light& l)
		{
			l.type = std::clamp(l.type, 0, 2);

			l.color[0] = Clamp(l.color[0], 0.0f, 1.0f);
			l.color[1] = Clamp(l.color[1], 0.0f, 1.0f);
			l.color[2] = Clamp(l.color[2], 0.0f, 1.0f);
			l.color[3] = Clamp(l.color[3], 0.0f, 1.0f);

			l.intensity = fmaxf(l.intensity, 0.0f);
			l.range = fmaxf(l.range, 0.1f);

			l.shadowRange = fmaxf(l.shadowRange, 0.1f);
			l.shadowBias = fmaxf(l.shadowBias, 0.0f);
			l.shadowStrength = Clamp(l.shadowStrength, 0.0f, 1.0f);
			l.shadowOrthoSize = fmaxf(l.shadowOrthoSize, 0.1f);
			l.shadowNear = fmaxf(l.shadowNear, 0.01f);
			l.shadowFar = fmaxf(l.shadowFar, l.shadowNear + 0.1f);
			l.shadowFovy = Clamp(l.shadowFovy, 1.0f, 179.0f);
		};

	if (tag == "LIGHTS_V2" || tag == "LIGHTS2" || tag == "LIGHTS3" || tag == "LIGHTS")
	{
		int count = std::min(savedLights, MAX_LIGHTS);

		for (int i = 0; i < savedLights; i++)
		{
			std::string line;
			std::getline(in >> std::ws, line);

			if (i >= MAX_LIGHTS)
				continue;

			std::istringstream ls(line);

			Light loaded = lights[i];

			if (!(ls >> std::quoted(loaded.name)))
			{
				TraceLog(
					LOG_WARNING,
					"Failed to read light name at light %i. Line: %s",
					i,
					line.c_str()
				);
				continue;
			}

			std::vector<float> v;
			float value = 0.0f;

			while (ls >> value)
			{
				v.push_back(value);
			}

			bool parsed = false;

			// ---------------------------------------------------------
			// Current existing file format:
			//
			// LIGHTS_V2
			// name type pos target intensity enabled color range shadow...
			//
			// Example:
			// "Yellow Light" 1 -0.15 5.65 0.5 0 0.9 0 2.15 0 ...
			// ---------------------------------------------------------
			if ((tag == "LIGHTS_V2" || tag == "LIGHTS2") && v.size() >= 22)
			{
				loaded.type = std::clamp(ToInt(v[0]), 0, 2);

				loaded.position = {
					v[1],
					v[2],
					v[3]
				};

				loaded.target = {
					v[4],
					v[5],
					v[6]
				};

				loaded.intensity = v[7];
				loaded.enabled = ToBoolInt(v[8]);

				loaded.color[0] = v[9];
				loaded.color[1] = v[10];
				loaded.color[2] = v[11];
				loaded.color[3] = v[12];

				loaded.range = v[13];
				loaded.castsShadow = ToBoolInt(v[14]) != 0;

				loaded.shadowRange = v[15];
				loaded.shadowBias = v[16];
				loaded.shadowStrength = v[17];
				loaded.shadowOrthoSize = v[18];
				loaded.shadowNear = v[19];
				loaded.shadowFar = v[20];
				loaded.shadowFovy = v[21];

				parsed = true;
			}

			// ---------------------------------------------------------
			// Optional future format:
			//
			// LIGHTS3
			// name type enabled pos target color intensity range shadow...
			// ---------------------------------------------------------
			else if (tag == "LIGHTS3" && v.size() >= 22)
			{
				loaded.type = std::clamp(ToInt(v[0]), 0, 2);
				loaded.enabled = ToBoolInt(v[1]);

				loaded.position = {
					v[2],
					v[3],
					v[4]
				};

				loaded.target = {
					v[5],
					v[6],
					v[7]
				};

				loaded.color[0] = v[8];
				loaded.color[1] = v[9];
				loaded.color[2] = v[10];
				loaded.color[3] = v[11];

				loaded.intensity = v[12];
				loaded.range = v[13];
				loaded.castsShadow = ToBoolInt(v[14]) != 0;

				loaded.shadowRange = v[15];
				loaded.shadowBias = v[16];
				loaded.shadowStrength = v[17];
				loaded.shadowOrthoSize = v[18];
				loaded.shadowNear = v[19];
				loaded.shadowFar = v[20];
				loaded.shadowFovy = v[21];

				parsed = true;
			}

			// ---------------------------------------------------------
			// Very old format:
			//
			// LIGHTS
			// name pos intensity enabled optionalColor
			// ---------------------------------------------------------
			else if (tag == "LIGHTS" && v.size() >= 5)
			{
				loaded.type = LIGHT_POINT;

				loaded.position = {
					v[0],
					v[1],
					v[2]
				};

				loaded.target = Vector3Add(
					loaded.position,
					Vector3{ 0.0f, -1.0f, 0.0f }
				);

				loaded.intensity = v[3];
				loaded.enabled = ToBoolInt(v[4]);

				if (v.size() >= 9)
				{
					loaded.color[0] = v[5];
					loaded.color[1] = v[6];
					loaded.color[2] = v[7];
					loaded.color[3] = v[8];
				}
				else
				{
					loaded.color[0] = 1.0f;
					loaded.color[1] = 1.0f;
					loaded.color[2] = 1.0f;
					loaded.color[3] = 1.0f;
				}

				loaded.range = fmaxf(loaded.range, 6.0f);

				parsed = true;
			}

			if (!parsed)
			{
				TraceLog(
					LOG_WARNING,
					"Failed to parse light %i. Tag=%s Values=%i Line=%s",
					i,
					tag.c_str(),
					(int)v.size(),
					line.c_str()
				);

				continue;
			}

			ClampLoadedLight(loaded);

			lights[i] = loaded;

			RefreshLightShaderLocations(i);
			UpdateLight(lights[i]);

			TraceLog(
				LOG_INFO,
				"Loaded light[%i] '%s': type=%i enabled=%i pos=(%.2f %.2f %.2f) target=(%.2f %.2f %.2f) intensity=%.2f color=(%.2f %.2f %.2f %.2f)",
				i,
				lights[i].name.c_str(),
				lights[i].type,
				lights[i].enabled,
				lights[i].position.x,
				lights[i].position.y,
				lights[i].position.z,
				lights[i].target.x,
				lights[i].target.y,
				lights[i].target.z,
				lights[i].intensity,
				lights[i].color[0],
				lights[i].color[1],
				lights[i].color[2],
				lights[i].color[3]
			);
		}

		lightCount = count;

		SetShaderValue(
			pbrShader,
			lightCountLoc,
			&lightCount,
			SHADER_UNIFORM_INT
		);

		for (int i = lightCount; i < MAX_LIGHTS; i++)
		{
			lights[i].enabled = 0;

			RefreshLightShaderLocations(i);
			UpdateLight(lights[i]);
		}

		MarkShadowMapsDirty();

		TraceLog(
			LOG_INFO,
			"Loaded lights section: tag=%s saved=%i active=%i",
			tag.c_str(),
			savedLights,
			lightCount
		);
	}
	else
	{
		TraceLog(
			LOG_WARNING,
			"Expected LIGHTS/LIGHTS_V2/LIGHTS2/LIGHTS3 section but found: %s",
			tag.c_str()
		);
	}

	size_t savedBoxes = 0;
	in >> tag >> savedBoxes;
	if (tag == "BLOCKOUT")
	{
		size_t count = std::min(savedBoxes, blockoutBoxes.size());
		for (size_t i = 0; i < count; i++)
		{
			in >> std::quoted(blockoutBoxes[i].name)
				>> blockoutBoxes[i].position.x
				>> blockoutBoxes[i].position.y
				>> blockoutBoxes[i].position.z
				>> blockoutBoxes[i].size.x
				>> blockoutBoxes[i].size.y
				>> blockoutBoxes[i].size.z
				>> blockoutBoxes[i].rotationDeg.x
				>> blockoutBoxes[i].rotationDeg.y
				>> blockoutBoxes[i].rotationDeg.z
				>> blockoutBoxes[i].hasCollision
				>> blockoutBoxes[i].blocksPlayer
				>> blockoutBoxes[i].visible
				>> blockoutBoxes[i].useNormalCollision
				>> blockoutBoxes[i].useJoltCollider;
		}

		for (size_t i = count; i < savedBoxes; i++)
		{
			std::string skipName;
			float px, py, pz, sx, sy, sz, rx, ry, rz;
			bool hc, bp, vis, legacy, jolt;
			in >> std::quoted(skipName)
				>> px >> py >> pz
				>> sx >> sy >> sz
				>> rx >> ry >> rz
				>> hc >> bp >> vis >> legacy >> jolt;
		}
	}



	while (in >> tag)
	{
		TraceLog(LOG_INFO, "LoadSceneState tag: %s", tag.c_str());

		if (tag == "SCENEPROPS")
		{
			size_t count = 0;
			in >> count;

			std::string dummyLine;
			std::getline(in, dummyLine); // finish current line

			for (size_t i = 0; i < count; i++)
			{
				std::getline(in, dummyLine);
			}

			continue;
		}


		if (tag == "SHADOWPERF")
		{
			in >> shadowMapsUpdateEveryFrame
				>> shadowMapsUpdateWhenDynamicPropsExist
				>> shadowMapsUpdateForCustomers;

			continue;
		}

		if (tag == "OUTLINE")
		{
			int r, g, b, a;

			in >> r >> g >> b >> a;
			pickupOutlineColor = {
				(unsigned char)r,
				(unsigned char)g,
				(unsigned char)b,
				(unsigned char)a
			};

			in >> pickupOutlineWidth;

			in >> pickupOutlinePulseEnabled;
			in >> pickupOutlinePulseMinWidth;
			in >> pickupOutlinePulseMaxWidth;
			in >> pickupOutlinePulseSpeed;
			in >> pickupOutlinePulseMinAlpha;
			in >> pickupOutlinePulseMaxAlpha;

			in >> pickupOccludedOutlineEnabled;
			in >> pickupOccludedOutlineWidth;

			in >> r >> g >> b >> a;
			pickupOccludedOutlineColor = {
				(unsigned char)r,
				(unsigned char)g,
				(unsigned char)b,
				(unsigned char)a
			};

			in >> pickupHiddenFillEnabled;
			in >> pickupHiddenFillWidth;

			in >> r >> g >> b >> a;
			pickupHiddenFillColor = {
				(unsigned char)r,
				(unsigned char)g,
				(unsigned char)b,
				(unsigned char)a
			};

			continue;
		}

		if (tag == "BLOOM")
		{
			in >> bloomEnabled
				>> bloomThreshold
				>> bloomKnee
				>> bloomIntensity
				>> bloomBlurPasses;

			bloomThreshold = Clamp(bloomThreshold, 0.1f, 1.5f);
			bloomKnee = Clamp(bloomKnee, 0.01f, 0.5f);
			bloomIntensity = Clamp(bloomIntensity, 0.0f, 2.0f);
			bloomBlurPasses = std::max(1, std::min(bloomBlurPasses, 8));

			continue;
		}


		if (tag == "SCENEPROP_INSPECT_DIALOGUE")
		{
			size_t count = 0;
			in >> count;

			std::unordered_map<std::string, int> indexByKey;

			for (int i = 0; i < (int)sceneProps.size(); i++)
			{
				indexByKey[GetScenePropStateKey(sceneProps[i], i)] = i;
			}

			for (size_t i = 0; i < count; i++)
			{
				std::string key;
				std::string inspectTag;
				size_t lineCount = 0;

				in
					>> std::quoted(key)
					>> std::quoted(inspectTag)
					>> lineCount;

				std::vector<std::string> lines;
				lines.reserve(lineCount);

				for (size_t lineIndex = 0; lineIndex < lineCount; lineIndex++)
				{
					std::string line;
					in >> std::quoted(line);
					lines.push_back(line);
				}

				auto found = indexByKey.find(key);

				if (found == indexByKey.end())
					continue;

				SceneProp& p = sceneProps[found->second];

				p.inspectDialogueTag = inspectTag;
				p.inspectDialogueLines = lines;
			}

			continue;
		}

		if (tag == "PHYSBOX")
		{
			in >> physicsBoxSavedPos.x
				>> physicsBoxSavedPos.y
				>> physicsBoxSavedPos.z
				>> physicsBoxSavedRot.x
				>> physicsBoxSavedRot.y
				>> physicsBoxSavedRot.z
				>> physicsBoxSavedRot.w;

			continue;
		}

		if (tag == "SCENEPROPS_V2")
		{
			size_t count = 0;
			in >> count;

			std::vector<ScenePropState> loadedStates;
			loadedStates.reserve(count);

			for (size_t i = 0; i < count; i++)
			{
				ScenePropState s{};

				in
					>> std::quoted(s.key)
					>> std::quoted(s.name)
					>> std::quoted(s.sourceGlbPath)
					>> std::quoted(s.sourceNodeName)
					>> s.sourceNodeIndex
					>> s.importedFromGlbScene
					>> s.parentIndex

					>> s.position.x >> s.position.y >> s.position.z
					>> s.rotationDeg.x >> s.rotationDeg.y >> s.rotationDeg.z
					>> s.scale.x >> s.scale.y >> s.scale.z

					>> s.localPosition.x >> s.localPosition.y >> s.localPosition.z
					>> s.localRotationDeg.x >> s.localRotationDeg.y >> s.localRotationDeg.z
					>> s.localScale.x >> s.localScale.y >> s.localScale.z

					>> s.visible
					>> s.lockUniformScale
					>> s.hasCollision
					>> s.blocksPlayer
					>> s.useNormalCollision
					>> s.useJoltCollider
					>> s.simulatePhysics
					>> s.syncFromPhysics
					>> s.editLockPhysics

					>> s.colliderOffset.x >> s.colliderOffset.y >> s.colliderOffset.z
					>> s.colliderSize.x >> s.colliderSize.y >> s.colliderSize.z;

				loadedStates.push_back(s);
			}

			ApplyScenePropStates(loadedStates);
			blockoutDirty = true;
			continue;
		}

		if (tag == "SCENEPROP_TRADE_TAGS")
		{
			size_t count = 0;
			in >> count;

			std::unordered_map<std::string, int> indexByKey;

			for (int i = 0; i < (int)sceneProps.size(); i++)
			{
				indexByKey[GetScenePropStateKey(sceneProps[i], i)] = i;
			}

			for (size_t i = 0; i < count; i++)
			{
				std::string key;
				int categoryValue = 0;
				std::string tradeItemId;
				std::string itemTag;

				in
					>> std::quoted(key)
					>> categoryValue
					>> std::quoted(tradeItemId)
					>> std::quoted(itemTag);

				auto found = indexByKey.find(key);

				if (found == indexByKey.end())
					continue;

				SceneProp& p = sceneProps[found->second];

				categoryValue = std::max(0, std::min(categoryValue, 3));

				p.pickupCategory = (PickupItemCategory)categoryValue;
				p.tradeItemId = tradeItemId;
				p.itemTag = itemTag;
			}

			continue;
		}

		if (tag == "SCENEPROP_SHADOWS")
		{
			size_t count = 0;
			in >> count;

			std::unordered_map<std::string, int> indexByKey;

			for (int i = 0; i < (int)sceneProps.size(); i++)
			{
				indexByKey[GetScenePropStateKey(sceneProps[i], i)] = i;
			}

			for (size_t i = 0; i < count; i++)
			{
				std::string key;
				bool castsShadow = true;

				in
					>> std::quoted(key)
					>> castsShadow;

				auto found = indexByKey.find(key);

				if (found == indexByKey.end())
					continue;

				SceneProp& p = sceneProps[found->second];
				p.castsShadow = castsShadow;
			}

			MarkShadowMapsDirty();
			continue;
		}

		if (tag == "SCENEPROP_COLLIDER_OVERRIDES")
		{
			size_t count = 0;
			in >> count;

			std::unordered_map<std::string, int> indexByKey;

			for (int i = 0; i < (int)sceneProps.size(); i++)
			{
				indexByKey[GetScenePropStateKey(sceneProps[i], i)] = i;
			}

			for (size_t i = 0; i < count; i++)
			{
				std::string key;
				bool manualOverride = false;

				Vector3 colliderOffset{};
				Vector3 colliderSize{};

				bool hasCollision = false;
				bool blocksPlayer = false;
				bool useJoltCollider = false;
				bool useNormalCollision = false;

				in
					>> std::quoted(key)
					>> manualOverride
					>> colliderOffset.x
					>> colliderOffset.y
					>> colliderOffset.z
					>> colliderSize.x
					>> colliderSize.y
					>> colliderSize.z
					>> hasCollision
					>> blocksPlayer
					>> useJoltCollider
					>> useNormalCollision;

				auto found = indexByKey.find(key);

				if (found == indexByKey.end())
					continue;

				SceneProp& p = sceneProps[found->second];

				p.manualColliderOverride = manualOverride;
				p.colliderOffset = colliderOffset;
				p.colliderSize = colliderSize;

				p.hasCollision = hasCollision;
				p.blocksPlayer = blocksPlayer;
				p.useJoltCollider = useJoltCollider;
				p.useNormalCollision = useNormalCollision;
			}

			continue;
		}

		if (tag == "ITEM_PLACEMENT_SPOTS")
		{
			size_t count = 0;
			in >> count;

			itemPlacementSpots.clear();
			itemPlacementSpots.reserve(count);

			for (size_t i = 0; i < count; i++)
			{
				ItemPlacementSpot spot{};
				int kind = 0;

				in
					>> std::quoted(spot.id)
					>> kind

					>> spot.position.x
					>> spot.position.y
					>> spot.position.z

					>> spot.rotationDeg.x
					>> spot.rotationDeg.y
					>> spot.rotationDeg.z

					>> spot.scale.x
					>> spot.scale.y
					>> spot.scale.z

					>> spot.snapRadius
					>> spot.enabled
					>> spot.allowPlayerDrop
					>> spot.allowCustomerPlace
					>> std::quoted(spot.acceptedItemTag);

				spot.kind = (ItemPlacementSpotKind)kind;
				spot.occupiedScenePropIndex = -1;

				itemPlacementSpots.push_back(spot);
			}

			RebuildItemPlacementSpotOccupancy();

			continue;
		}

		if (tag == "SCENEPROP_PLACEMENT")
		{
			size_t count = 0;
			in >> count;

			std::unordered_map<std::string, int> indexByKey;

			for (int i = 0; i < (int)sceneProps.size(); i++)
			{
				indexByKey[GetScenePropStateKey(sceneProps[i], i)] = i;
			}

			for (size_t i = 0; i < count; i++)
			{
				std::string key;
				std::string itemTag;
				std::string currentSpotId;
				std::string homeSpotId;
				bool preferHome = true;
				bool placedByCustomer = false;

				in
					>> std::quoted(key)
					>> std::quoted(itemTag)
					>> std::quoted(currentSpotId)
					>> std::quoted(homeSpotId)
					>> preferHome
					>> placedByCustomer;

				auto found = indexByKey.find(key);

				if (found == indexByKey.end())
					continue;

				SceneProp& p = sceneProps[found->second];

				p.itemTag = itemTag;
				p.currentPlacementSpotIndex = FindItemPlacementSpotById(currentSpotId);
				p.homePlacementSpotIndex = FindItemPlacementSpotById(homeSpotId);
				p.preferHomePlacement = preferHome;
				p.placedByCustomer = placedByCustomer;
			}

			RebuildItemPlacementSpotOccupancy();

			continue;
		}

		if (tag == "IMPORTED_PROP_OFFSETS")
		{
			size_t count = 0;
			in >> count;

			std::unordered_map<std::string, int> indexByKey;

			for (int i = 0; i < (int)sceneProps.size(); i++)
			{
				indexByKey[GetScenePropStateKey(sceneProps[i], i)] = i;
			}

			for (size_t i = 0; i < count; i++)
			{
				std::string key;
				Vector3 offset{};
				Vector3 rot{};
				Vector3 scale{ 1.0f, 1.0f, 1.0f };

				in
					>> std::quoted(key)
					>> offset.x >> offset.y >> offset.z
					>> rot.x >> rot.y >> rot.z
					>> scale.x >> scale.y >> scale.z;

				auto found = indexByKey.find(key);

				if (found == indexByKey.end())
					continue;

				SceneProp& p = sceneProps[found->second];

				p.importedEditorOffset = offset;
				p.importedEditorRotationDeg = rot;
				p.importedEditorScale = scale;

				ApplyImportedEditorTransformToRuntime(p);
			}

			continue;
		}

		if (tag == "SCENEPROP_OCCLUSION")
		{
			size_t count = 0;
			in >> count;

			std::unordered_map<std::string, int> indexByKey;

			for (int i = 0; i < (int)sceneProps.size(); i++)
			{
				indexByKey[GetScenePropStateKey(sceneProps[i], i)] = i;
			}

			for (size_t i = 0; i < count; i++)
			{
				std::string key;
				bool canOcclude = false;
				bool ignoreOcclusionCulling = false;

				in
					>> std::quoted(key)
					>> canOcclude
					>> ignoreOcclusionCulling;

				auto found = indexByKey.find(key);

				if (found == indexByKey.end())
					continue;

				SceneProp& p = sceneProps[found->second];

				p.canOcclude = canOcclude;
				p.ignoreOcclusionCulling = ignoreOcclusionCulling;
			}

			continue;
		}

		if (tag == "PICKUP_HOLD")
		{
			size_t count = 0;
			in >> count;

			std::unordered_map<std::string, int> indexByKey;

			for (int i = 0; i < (int)sceneProps.size(); i++)
			{
				indexByKey[GetScenePropStateKey(sceneProps[i], i)] = i;
			}

			for (size_t i = 0; i < count; i++)
			{
				std::string key;
				bool canPickup = false;

				Vector3 holdOffset{};
				Vector3 holdRot{};
				bool followPitch = false;
				bool snapUpright = true;
				Vector3 dropRot{};

				in
					>> std::quoted(key)
					>> canPickup

					>> holdOffset.x
					>> holdOffset.y
					>> holdOffset.z

					>> holdRot.x
					>> holdRot.y
					>> holdRot.z

					>> followPitch
					>> snapUpright

					>> dropRot.x
					>> dropRot.y
					>> dropRot.z;

				auto found = indexByKey.find(key);

				if (found == indexByKey.end())
					continue;

				SceneProp& p = sceneProps[found->second];

				p.canPickup = canPickup;
				p.holdOffsetLocal = holdOffset;
				p.holdRotationOffsetDeg = holdRot;
				p.holdFollowCameraPitch = followPitch;
				p.snapUprightOnDrop = snapUpright;
				p.dropRotationOffsetDeg = dropRot;

				if (p.canPickup)
				{
					p.hasCollision = true;
					p.blocksPlayer = true;
					p.useJoltCollider = true;
					p.useNormalCollision = false;
					p.simulatePhysics = true;
					p.syncFromPhysics = false;
					p.editLockPhysics = true;
				}
			}

			continue;
		}

		if (tag == "INSTANCEDPROPS")
		{
			size_t count = 0;
			in >> count;

			std::vector<InstancedPropState> loaded;
			loaded.reserve(count);

			for (size_t i = 0; i < count; i++)
			{
				InstancedPropState s{};

				in >> std::quoted(s.name)
					>> s.type
					>> s.position.x >> s.position.y >> s.position.z
					>> s.rotationDeg.x >> s.rotationDeg.y >> s.rotationDeg.z
					>> s.scale.x >> s.scale.y >> s.scale.z
					>> s.visible;

				loaded.push_back(s);
			}

			ApplyInstancedPropStates(loaded);

			TraceLog(
				LOG_INFO,
				"Loaded %i instanced props from scene_state.txt.",
				(int)loaded.size()
			);

			continue;
		}

		if (tag == "CUSTOMER_POIS")
		{
			size_t poiCount = 0;
			in >> poiCount;

			customerPOIs.clear();

			for (size_t i = 0; i < poiCount; i++)
			{
				CustomerPOI poi;
				float oldWaitSeconds = 1.5f;

				in
					>> std::quoted(poi.id)
					>> std::quoted(poi.group)
					>> poi.position.x
					>> poi.position.y
					>> poi.position.z
					>> poi.radius
					>> oldWaitSeconds
					>> poi.enabled
					>> poi.stopPoint;

				poi.waitSecondsMin = oldWaitSeconds;
				poi.waitSecondsMax = oldWaitSeconds;

				customerPOIs.push_back(poi);
			}

			selectedCustomerPOI = -1;
			customerNavGridDirty = true;
			StopAllCustomerRoaming();

			TraceLog(LOG_INFO, "Loaded %i old customer POIs from scene_state.txt.", (int)customerPOIs.size());

			continue;
		}

		if (tag == "CUSTOMER_POIS_V3")
		{
			size_t poiCount = 0;
			in >> poiCount;

			customerPOIs.clear();

			for (size_t i = 0; i < poiCount; i++)
			{
				CustomerPOI poi;
				int kindValue = 0;

				in
					>> std::quoted(poi.id)
					>> std::quoted(poi.group)
					>> poi.position.x
					>> poi.position.y
					>> poi.position.z
					>> poi.radius
					>> poi.waitSecondsMin
					>> poi.waitSecondsMax
					>> poi.enabled
					>> poi.stopPoint
					>> poi.exclusive
					>> poi.capacity
					>> kindValue
					>> poi.queueOrder
					>> poi.useFacingDirection
					>> poi.facingYawDeg;

				if (in.fail())
					break;

				poi.kind = (CustomerPOIKind)kindValue;
				customerPOIs.push_back(poi);
			}

			selectedCustomerPOI = -1;
			customerNavGridDirty = true;
			StopAllCustomerRoaming();

			TraceLog(
				LOG_INFO,
				"Loaded %i customer POIs V3 from scene_state.txt.",
				(int)customerPOIs.size()
			);

			continue;
		}

		if (tag == "CUSTOMER_POIS_V2")
		{
			size_t poiCount = 0;
			in >> poiCount;

			customerPOIs.clear();

			for (size_t i = 0; i < poiCount; i++)
			{
				CustomerPOI poi;
				int kindValue = 0;

				in
					>> std::quoted(poi.id)
					>> std::quoted(poi.group)
					>> poi.position.x
					>> poi.position.y
					>> poi.position.z
					>> poi.radius
					>> poi.waitSecondsMin
					>> poi.waitSecondsMax
					>> poi.enabled
					>> poi.stopPoint
					>> poi.exclusive
					>> poi.capacity
					>> kindValue
					>> poi.queueOrder;

				poi.kind = (CustomerPOIKind)kindValue;

				customerPOIs.push_back(poi);
			}

			selectedCustomerPOI = -1;
			customerNavGridDirty = true;
			StopAllCustomerRoaming();

			TraceLog(LOG_INFO, "Loaded %i customer POIs V2 from scene_state.txt.", (int)customerPOIs.size());

			continue;
		}

		if (tag == "CUSTOMERS")
		{
			size_t count = 0;
			in >> count;

			std::vector<CustomerState> loadedCustomers;
			loadedCustomers.reserve(count);

			for (size_t i = 0; i < count; i++)
			{
				CustomerState s{};

				in
					>> s.index
					>> s.position.x >> s.position.y >> s.position.z
					>> s.targetPosition.x >> s.targetPosition.y >> s.targetPosition.z
					>> s.yawDeg
					>> s.scale.x >> s.scale.y >> s.scale.z
					>> s.moveSpeed
					>> s.usePOINavigation
					>> s.editorFrozen
					>> s.currentPOIIndex
					>> s.targetPOIIndex
					>> s.destinationPOIIndex
					>> std::quoted(s.dialogueScriptId)
					>> std::quoted(s.poiGroup);

				loadedCustomers.push_back(s);
			}

			ApplyCustomerStates(loadedCustomers);
			continue;
		}

		if (tag == "CINEMATIC_TRIGGERS_V1")
		{
			size_t count = 0;
			in >> count;

			cinematicTriggers.clear();
			cinematicTriggers.reserve(count);

			for (size_t i = 0; i < count; i++)
			{
				CinematicTriggerZone trigger{};

				in
					>> std::quoted(trigger.id)

					>> trigger.position.x
					>> trigger.position.y
					>> trigger.position.z

					>> trigger.size.x
					>> trigger.size.y
					>> trigger.size.z

					>> trigger.lookTarget.x
					>> trigger.lookTarget.y
					>> trigger.lookTarget.z

					>> trigger.duration

					>> trigger.enabled
					>> trigger.repeatable
					>> trigger.lockPlayerMovement
					>> trigger.syncPlayerLookAtEnd;

				trigger.triggered = false;
				trigger.wasInside = false;

				cinematicTriggers.push_back(trigger);
			}

			selectedCinematicTriggerIndex = -1;

			TraceLog(
				LOG_INFO,
				"Loaded %i cinematic triggers from scene_state.txt.",
				(int)cinematicTriggers.size()
			);

			continue;
		}

		if (tag == "CINEMATIC_TRIGGERS_V4")
		{
			size_t count = 0;
			in >> count;

			cinematicTriggers.clear();
			cinematicTriggers.reserve(count);

			for (size_t i = 0; i < count; i++)
			{
				CinematicTriggerZone trigger{};
				std::string joinedDialogue;

				in
					>> std::quoted(trigger.id)

					>> trigger.position.x
					>> trigger.position.y
					>> trigger.position.z

					>> trigger.size.x
					>> trigger.size.y
					>> trigger.size.z

					>> trigger.lookTarget.x
					>> trigger.lookTarget.y
					>> trigger.lookTarget.z

					>> trigger.duration

					>> trigger.enabled
					>> trigger.repeatable
					>> trigger.lockPlayerMovement
					>> trigger.syncPlayerLookAtEnd

					>> trigger.enableLookSound
					>> std::quoted(trigger.lookSoundSfxId)
					>> trigger.lookSoundDistance
					>> trigger.lookSoundLoop
					>> trigger.lookSoundVolume
					>> trigger.lookSoundFadeInSeconds
					>> trigger.lookSoundFadeOutSeconds

					>> trigger.playSelfDialogue
					>> std::quoted(trigger.selfDialogueVoicePrefix)
					>> std::quoted(joinedDialogue);

				trigger.selfDialogueLines = SplitLines(joinedDialogue);

				trigger.triggered = false;
				trigger.wasInside = false;

				cinematicTriggers.push_back(trigger);
			}

			selectedCinematicTriggerIndex = -1;
			continue;
		}

		if (tag == "CINEMATIC_TRIGGERS_V3")
		{
			size_t count = 0;
			in >> count;

			cinematicTriggers.clear();
			cinematicTriggers.reserve(count);

			for (size_t i = 0; i < count; i++)
			{
				CinematicTriggerZone trigger{};
				std::string joinedDialogue;

				in
					>> std::quoted(trigger.id)

					>> trigger.position.x
					>> trigger.position.y
					>> trigger.position.z

					>> trigger.size.x
					>> trigger.size.y
					>> trigger.size.z

					>> trigger.lookTarget.x
					>> trigger.lookTarget.y
					>> trigger.lookTarget.z

					>> trigger.duration

					>> trigger.enabled
					>> trigger.repeatable
					>> trigger.lockPlayerMovement
					>> trigger.syncPlayerLookAtEnd

					>> trigger.enableLookSound
					>> std::quoted(trigger.lookSoundSfxId)
					>> trigger.lookSoundDistance
					>> trigger.lookSoundLoop
					>> trigger.lookSoundVolume
					>> trigger.lookSoundFadeInSeconds
					>> trigger.lookSoundFadeOutSeconds

					>> trigger.playSelfDialogue
					>> std::quoted(trigger.selfDialogueVoicePrefix)
					>> std::quoted(joinedDialogue);

				trigger.selfDialogueLines = SplitLines(joinedDialogue);

				trigger.triggered = false;
				trigger.wasInside = false;

				cinematicTriggers.push_back(trigger);
			}

			selectedCinematicTriggerIndex = -1;
			continue;
		}

		if (tag == "CINEMATIC_TRIGGERS_V2")
		{
			size_t count = 0;
			in >> count;

			cinematicTriggers.clear();
			cinematicTriggers.reserve(count);

			for (size_t i = 0; i < count; i++)
			{
				CinematicTriggerZone trigger{};
				std::string joinedDialogue;

				in
					>> std::quoted(trigger.id)

					>> trigger.position.x
					>> trigger.position.y
					>> trigger.position.z

					>> trigger.size.x
					>> trigger.size.y
					>> trigger.size.z

					>> trigger.lookTarget.x
					>> trigger.lookTarget.y
					>> trigger.lookTarget.z

					>> trigger.duration

					>> trigger.enabled
					>> trigger.repeatable
					>> trigger.lockPlayerMovement
					>> trigger.syncPlayerLookAtEnd

					>> trigger.playSelfDialogue
					>> std::quoted(trigger.selfDialogueVoicePrefix)
					>> std::quoted(joinedDialogue);

				trigger.selfDialogueLines = SplitLines(joinedDialogue);

				trigger.triggered = false;
				trigger.wasInside = false;

				cinematicTriggers.push_back(trigger);
			}

			selectedCinematicTriggerIndex = -1;
			continue;
		}

		if (tag == "DIALOGUE_SETTINGS_V1")
		{
			in >> customerDialogueAutoPlayEnabled;
			continue;
		}

		TraceLog(LOG_WARNING, "Unknown scene_state tag: %s", tag.c_str());
		break;
	}





	blockoutDirty = true;
	return true;
}

std::string Game::GetScenePropStateKey(const SceneProp& prop, int index) const
{
	if (prop.importedFromGlbScene)
	{
		std::string nodeName = !prop.sourceNodeName.empty()
			? prop.sourceNodeName
			: prop.name;

		return "GLB_NODE|" + nodeName;
	}

	return "MANUAL|" + prop.name + "|" + std::to_string(index);
}


//edit - do undo

Game::SceneSnapshot Game::CaptureSceneSnapshot() const
{
	SceneSnapshot s;
	s.floorName = floorName;
	s.propName = propName;
	s.physicsBoxName = physicsBoxName;

	s.floorPos = floorDrawPos;
	s.floorRotDeg = floorDrawRotDeg;
	s.propPos = propDrawPos;
	s.propRotDeg = propDrawRotDeg;

	s.blockoutBoxes = blockoutBoxes;
	s.lightCount = lightCount;
	s.sceneProps = CaptureScenePropStates();
	s.instancedProps = CaptureInstancedPropStates();
	s.itemPlacementSpots = itemPlacementSpots;

	s.pickupOutlineColor = pickupOutlineColor;
	s.pickupOutlineWidth = pickupOutlineWidth;

	s.pickupOutlinePulseEnabled = pickupOutlinePulseEnabled;
	s.pickupOutlinePulseMinWidth = pickupOutlinePulseMinWidth;
	s.pickupOutlinePulseMaxWidth = pickupOutlinePulseMaxWidth;
	s.pickupOutlinePulseSpeed = pickupOutlinePulseSpeed;
	s.pickupOutlinePulseMinAlpha = pickupOutlinePulseMinAlpha;
	s.pickupOutlinePulseMaxAlpha = pickupOutlinePulseMaxAlpha;

	s.pickupOccludedOutlineEnabled = pickupOccludedOutlineEnabled;
	s.pickupOccludedOutlineWidth = pickupOccludedOutlineWidth;
	s.pickupOccludedOutlineColor = pickupOccludedOutlineColor;

	s.pickupHiddenFillEnabled = pickupHiddenFillEnabled;
	s.pickupHiddenFillWidth = pickupHiddenFillWidth;
	s.pickupHiddenFillColor = pickupHiddenFillColor;

	s.shadowMapsUpdateEveryFrame = shadowMapsUpdateEveryFrame;
	s.shadowMapsUpdateWhenDynamicPropsExist = shadowMapsUpdateWhenDynamicPropsExist;
	s.shadowMapsUpdateForCustomers = shadowMapsUpdateForCustomers;

	s.customerPOIs = customerPOIs;
	s.customers = CaptureCustomerStates();
	s.cinematicTriggers = cinematicTriggers;
	s.selectedCinematicTriggerIndex = selectedCinematicTriggerIndex;

	for (int i = 0; i < lightCount; i++)
		s.lights[i] = lights[i];

	if (hasTestDynamicBox)
	{
		s.physicsBoxPos = physics->GetBodyPosition(testDynamicBox);
		s.physicsBoxRot = physics->GetBodyRotation(testDynamicBox);
	}
	else
	{
		s.physicsBoxPos = physicsBoxSavedPos;
		s.physicsBoxRot = physicsBoxSavedRot;
	}

	return s;
}

void Game::ApplySceneSnapshot(const SceneSnapshot& s)
{
	floorName = s.floorName;
	propName = s.propName;
	physicsBoxName = s.physicsBoxName;

	floorDrawPos = s.floorPos;
	floorDrawRotDeg = s.floorRotDeg;
	propDrawPos = s.propPos;
	propDrawRotDeg = s.propRotDeg;

	blockoutBoxes = s.blockoutBoxes;
	lightCount = s.lightCount;

	pickupOutlineColor = s.pickupOutlineColor;
	pickupOutlineWidth = s.pickupOutlineWidth;

	pickupOutlinePulseEnabled = s.pickupOutlinePulseEnabled;
	pickupOutlinePulseMinWidth = s.pickupOutlinePulseMinWidth;
	pickupOutlinePulseMaxWidth = s.pickupOutlinePulseMaxWidth;
	pickupOutlinePulseSpeed = s.pickupOutlinePulseSpeed;
	pickupOutlinePulseMinAlpha = s.pickupOutlinePulseMinAlpha;
	pickupOutlinePulseMaxAlpha = s.pickupOutlinePulseMaxAlpha;

	pickupOccludedOutlineEnabled = s.pickupOccludedOutlineEnabled;
	pickupOccludedOutlineWidth = s.pickupOccludedOutlineWidth;
	pickupOccludedOutlineColor = s.pickupOccludedOutlineColor;

	pickupHiddenFillEnabled = s.pickupHiddenFillEnabled;
	pickupHiddenFillWidth = s.pickupHiddenFillWidth;
	pickupHiddenFillColor = s.pickupHiddenFillColor;
	ApplyScenePropStates(s.sceneProps);
	ApplyCustomerStates(s.customers);
	ApplyInstancedPropStates(s.instancedProps);
	itemPlacementSpots = s.itemPlacementSpots;
	selectedItemPlacementSpotIndex = -1;
	RebuildItemPlacementSpotOccupancy();

	customerPOIs = s.customerPOIs;
	selectedCustomerPOI = -1;
	customerNavGridDirty = true;
	StopAllCustomerRoaming();

	cinematicTriggers = s.cinematicTriggers;
	selectedCinematicTriggerIndex = s.selectedCinematicTriggerIndex;

	shadowMapsUpdateEveryFrame = s.shadowMapsUpdateEveryFrame;
	shadowMapsUpdateWhenDynamicPropsExist = s.shadowMapsUpdateWhenDynamicPropsExist;
	shadowMapsUpdateForCustomers = s.shadowMapsUpdateForCustomers;


	MarkShadowMapsDirty();

	for (int i = 0; i < lightCount; i++)
	{
		lights[i] = s.lights[i];

		RefreshLightShaderLocations(i);
		UpdateLight(lights[i]);
	}

	SetShaderValue(
		pbrShader,
		lightCountLoc,
		&lightCount,
		SHADER_UNIFORM_INT
	);

	for (int i = lightCount; i < MAX_LIGHTS; i++)
	{
		lights[i].enabled = 0;

		RefreshLightShaderLocations(i);
		UpdateLight(lights[i]);
	}

	MarkShadowMapsDirty();

	physicsBoxSavedPos = s.physicsBoxPos;
	physicsBoxSavedRot = s.physicsBoxRot;

	RecreatePhysicsWorld();
	physics->SetBodyRotation(testDynamicBox, physicsBoxSavedRot);
}
void Game::PushUndoSnapshot()
{
	undoStack.emplace_back(CaptureSceneSnapshot());
	if (undoStack.size() > 64)
		undoStack.erase(undoStack.begin());

	redoStack.clear();
}

void Game::HandleUndoRedo()
{
	if (!editMode) return;

	bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
	bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

	if (ctrl && !shift && IsKeyPressed(KEY_Z))
	{
		if (!undoStack.empty())
		{
			redoStack.emplace_back(CaptureSceneSnapshot());

			SceneSnapshot snap = undoStack.back();
			undoStack.pop_back();
			ApplySceneSnapshot(snap);
		}
	}

	if (ctrl && shift && IsKeyPressed(KEY_Z))
	{
		if (!redoStack.empty())
		{
			undoStack.emplace_back(CaptureSceneSnapshot());

			SceneSnapshot snap = redoStack.back();
			redoStack.pop_back();
			ApplySceneSnapshot(snap);
		}
	}
}
void Game::PushUndoIfItemActivated()
{
	if (ImGui::IsItemActivated())
	{
		PushUndoSnapshot();
	}
}


void Game::DrawEditorItemList()
{
	ImGui::Text("Selected Item");

	for (int i = 0; i < (int)editorItems.size(); i++)
	{
		EditorItem& item = editorItems[i];

		std::string label = item.name ? *item.name : ("Item " + std::to_string(i));
		label += "##EditorItem" + std::to_string(i);

		if (ImGui::Selectable(label.c_str(), selectedEditorItemIndex == i))
			selectedEditorItemIndex = i;
	}
}


void Game::RebuildEditorItems()
{
	editorItems.clear();
	editorItems.reserve(2 + lightCount + (int)blockoutBoxes.size() + (hasTestDynamicBox ? 1 : 0));

	{
		EditorItem item;
		item.type = EditorItemType::Floor;
		item.index = 0;
		item.name = &floorName;
		item.position = &floorDrawPos;
		item.rotation = &floorDrawRotDeg;
		editorItems.emplace_back(item);
	}

	{
		EditorItem item;
		item.type = EditorItemType::Prop;
		item.index = 0;
		item.name = &propName;
		item.position = &propDrawPos;
		item.rotation = &propDrawRotDeg;
		editorItems.emplace_back(item);
	}

	for (int i = 0; i < lightCount; i++)
	{
		EditorItem item;
		item.type = EditorItemType::Light;
		item.index = i;
		item.name = &lights[i].name;
		item.position = &lights[i].position;
		item.intensity = &lights[i].intensity;
		editorItems.emplace_back(item);
	}

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		EditorItem item;
		item.type = EditorItemType::SceneProp;
		item.index = i;
		item.name = &sceneProps[i].name;
		item.position = &sceneProps[i].position;
		item.rotation = &sceneProps[i].rotationDeg;
		item.visible = &sceneProps[i].visible;
		item.useLegacyBoxCollision = &sceneProps[i].useNormalCollision;
		item.useJoltCollider = &sceneProps[i].useJoltCollider;
		editorItems.emplace_back(item);
	}

	for (int i = 0; i < (int)blockoutBoxes.size(); i++)
	{
		EditorItem item;
		item.type = EditorItemType::Blockout;
		item.index = i;
		item.name = &blockoutBoxes[i].name;
		item.position = &blockoutBoxes[i].position;
		item.rotation = &blockoutBoxes[i].rotationDeg;
		item.size = &blockoutBoxes[i].size;
		item.visible = &blockoutBoxes[i].visible;
		item.hasCollision = &blockoutBoxes[i].hasCollision;
		item.useLegacyBoxCollision = &blockoutBoxes[i].useNormalCollision;
		item.useJoltCollider = &blockoutBoxes[i].useJoltCollider;
		editorItems.emplace_back(item);
	}

	if (hasTestDynamicBox)
	{
		EditorItem item;
		item.type = EditorItemType::PhysicsBox;
		item.index = 0;
		item.name = &physicsBoxName;
		editorItems.emplace_back(item);
	}

	if (selectedEditorItemIndex >= (int)editorItems.size())
		selectedEditorItemIndex = editorItems.empty() ? -1 : (int)editorItems.size() - 1;
}



void Game::DrawEditorItemInspector(EditorItem& item)
{
	if (item.name)
	{
		if (DrawEditableName("Name", *item.name))
		{
		}
		PushUndoIfItemActivated();
	}

	switch (item.type)
	{
	case EditorItemType::Floor:
	{
		ImGui::Text("Floor");

		if (ImGui::DragFloat3("Position", &item.position->x, 0.05f))
		{
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat3("Rotation", &item.rotation->x, 0.5f))
		{
		}
		PushUndoIfItemActivated();
		break;
	}

	case EditorItemType::Prop:
	{
		ImGui::Text("Prop");

		if (ImGui::DragFloat3("Position", &item.position->x, 0.05f))
		{
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat3("Rotation", &item.rotation->x, 0.5f))
		{
		}
		PushUndoIfItemActivated();


		break;
	}

	case EditorItemType::Light:
	{
		DrawLightInspector(item.index);
		break;
	}

	case EditorItemType::Blockout:
	{
		BlockoutBox& box = blockoutBoxes[item.index];
		ImGui::Text("Blockout %d", item.index);

		if (ImGui::DragFloat3("Position", &box.position.x, 0.05f))
		{
			blockoutDirty = true;
			MarkShadowMapsDirty();
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat3("Size", &box.size.x, 0.05f, 0.05f, 100.0f))
			blockoutDirty = true;
		PushUndoIfItemActivated();

		if (ImGui::DragFloat3("Rotation", &box.rotationDeg.x, 0.5f))
			blockoutDirty = true;
		PushUndoIfItemActivated();

		bool rotated =
			fabsf(box.rotationDeg.x) > 0.01f ||
			fabsf(box.rotationDeg.y) > 0.01f ||
			fabsf(box.rotationDeg.z) > 0.01f;

		if (rotated)
		{
			box.useJoltCollider = true;
			box.useNormalCollision = false;
		}

		if (rotated)
		{
			ImGui::BeginDisabled();
			ImGui::Checkbox("Use Legacy Box Collision", &box.useNormalCollision);
			ImGui::EndDisabled();
		}
		else
		{
			if (ImGui::Checkbox("Use Legacy Box Collision", &box.useNormalCollision))
				blockoutDirty = true;
			PushUndoIfItemActivated();
		}

		if (ImGui::Checkbox("Use Jolt Collider", &box.useJoltCollider))
			blockoutDirty = true;
		PushUndoIfItemActivated();

		if (ImGui::Checkbox("Visible", &box.visible))
		{
		}
		PushUndoIfItemActivated();

		if (ImGui::Checkbox("Has Collision", &box.hasCollision))
			blockoutDirty = true;
		PushUndoIfItemActivated();

		break;
	}
	case EditorItemType::SceneProp:
	{
		SceneProp& prop = sceneProps[item.index];
		ImGui::Text("Scene Prop %d", item.index);

		const bool hasBody = !prop.bodyId.IsInvalid();
		const bool livePhysics = prop.simulatePhysics && hasBody && !prop.editLockPhysics && !blockoutDirty;
		const bool lockedPhysics = prop.simulatePhysics && hasBody && prop.editLockPhysics && !blockoutDirty;

		if (livePhysics)
		{
			ReadScenePropTransformFromBody(prop);
		}

		if (ImGui::Checkbox("Lock Physics While Editing", &prop.editLockPhysics))
		{
			if (prop.simulatePhysics && hasBody && !blockoutDirty)
			{
				if (prop.editLockPhysics)
				{
					physics->SetBodyMotionType(prop.bodyId, JPH::EMotionType::Kinematic);
					physics->SetBodyLinearVelocity(prop.bodyId, { 0.0f, 0.0f, 0.0f });
					physics->SetBodyAngularVelocity(prop.bodyId, { 0.0f, 0.0f, 0.0f });
					prop.syncFromPhysics = false;
					ReadScenePropTransformFromBody(prop);
				}
				else
				{
					ApplyScenePropTransformToBody(prop);
					physics->SetBodyMotionType(prop.bodyId, JPH::EMotionType::Dynamic);
					physics->SetBodyLinearVelocity(prop.bodyId, { 0.0f, 0.0f, 0.0f });
					physics->SetBodyAngularVelocity(prop.bodyId, { 0.0f, 0.0f, 0.0f });
					prop.syncFromPhysics = true;
				}
			}
		}
		PushUndoIfItemActivated();

		const bool canEditTransform = !prop.simulatePhysics || lockedPhysics || !hasBody;

		if (!canEditTransform)
		{
			ImGui::BeginDisabled();
		}

		if (ImGui::DragFloat3("Position", &prop.position.x, 0.05f))
		{
			if (lockedPhysics)
				ApplyScenePropTransformToBody(prop);
			else
				blockoutDirty = true;
			MarkShadowMapsDirty();
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat3("Rotation", &prop.rotationDeg.x, 0.5f))
		{
			if (lockedPhysics)
				ApplyScenePropTransformToBody(prop);
			else
				blockoutDirty = true;
			MarkShadowMapsDirty();
		}
		PushUndoIfItemActivated();

		if (!canEditTransform)
		{
			ImGui::EndDisabled();
			ImGui::Text("Unlock simulation or enable lock to edit transform.");
		}

		if (ImGui::Checkbox("Lock Uniform Scale", &prop.lockUniformScale))
		{
		}
		PushUndoIfItemActivated();

		if (prop.lockUniformScale)
		{
			float uniform = prop.scale.x;
			if (ImGui::DragFloat("Uniform Scale", &uniform, 0.05f, 0.01f, 100.0f))
			{
				prop.scale = { uniform, uniform, uniform };
				blockoutDirty = true;
				MarkShadowMapsDirty();
			}
			PushUndoIfItemActivated();
		}
		else
		{
			if (ImGui::DragFloat3("Scale", &prop.scale.x, 0.05f, 0.01f, 100.0f))
			{
				blockoutDirty = true;
				MarkShadowMapsDirty();
			}
			PushUndoIfItemActivated();
		}

		if (ImGui::Checkbox("Visible", &prop.visible))
		{
		}
		PushUndoIfItemActivated();

		if (ImGui::Checkbox("Simulate Physics", &prop.simulatePhysics))
		{
			if (prop.simulatePhysics)
			{
				prop.useJoltCollider = true;
				prop.useNormalCollision = false;
			}
			prop.syncFromPhysics = prop.simulatePhysics && !prop.editLockPhysics;
			blockoutDirty = true;
		}
		PushUndoIfItemActivated();

		ImGui::Separator();
		ImGui::Text("Collider");

		if (ImGui::Checkbox("Has Collision", &prop.hasCollision))
		{
			blockoutDirty = true;
		}
		PushUndoIfItemActivated();

		if (ImGui::Checkbox("Blocks Player", &prop.blocksPlayer))
		{
			blockoutDirty = true;
		}
		PushUndoIfItemActivated();

		if (ImGui::Checkbox("Use Legacy Box Collision", &prop.useNormalCollision))
		{
			if (prop.useNormalCollision)
			{
				prop.useJoltCollider = false;

				if (prop.simulatePhysics)
				{
					prop.simulatePhysics = false;
					prop.syncFromPhysics = false;
				}
			}

			blockoutDirty = true;
		}
		PushUndoIfItemActivated();

		if (ImGui::Checkbox("Use Jolt Collider", &prop.useJoltCollider))
		{
			if (prop.useJoltCollider)
			{
				prop.useNormalCollision = false;
			}

			blockoutDirty = true;
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat3("Collider Offset", &prop.colliderOffset.x, 0.05f))
		{
			if (!prop.bodyId.IsInvalid() && !blockoutDirty)
			{
				ApplyScenePropTransformToBody(prop);
			}
			else
			{
				blockoutDirty = true;
			}
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat3("Collider Size", &prop.colliderSize.x, 0.05f, 0.01f, 100.0f))
		{
			prop.colliderSize.x = fmaxf(prop.colliderSize.x, 0.01f);
			prop.colliderSize.y = fmaxf(prop.colliderSize.y, 0.01f);
			prop.colliderSize.z = fmaxf(prop.colliderSize.z, 0.01f);

			blockoutDirty = true;
		}
		PushUndoIfItemActivated();

		if (ImGui::Button("Reset Collider To Model Bounds"))
		{
			PushUndoSnapshot();

			ResetScenePropColliderToModelBounds(prop);

			blockoutDirty = false;
		}

		break;
	}
	case EditorItemType::PhysicsBox:
	{
		if (!hasTestDynamicBox) break;

		Vector3 p = physics->GetBodyPosition(testDynamicBox);
		Quaternion q = physics->GetBodyRotation(testDynamicBox);
		Vector3 rotDeg = QuaternionToEuler(q);
		rotDeg.x *= RAD2DEG;
		rotDeg.y *= RAD2DEG;
		rotDeg.z *= RAD2DEG;

		ImGui::Text("Physics Box");

		if (ImGui::DragFloat3("Position", &p.x, 0.05f))
		{
			physics->SetBodyPosition(testDynamicBox, p);
			physics->SetBodyLinearVelocity(testDynamicBox, { 0.0f, 0.0f, 0.0f });
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat3("Rotation", &rotDeg.x, 0.5f))
		{
			Quaternion newQ = QuaternionFromEuler(
				rotDeg.x * DEG2RAD,
				rotDeg.y * DEG2RAD,
				rotDeg.z * DEG2RAD
			);
			physics->SetBodyRotation(testDynamicBox, newQ);
			physics->SetBodyLinearVelocity(testDynamicBox, { 0.0f, 0.0f, 0.0f });
		}
		PushUndoIfItemActivated();
		break;
	}
	}
}

//edit mode end

void Game::CreateVirtualPlayer()
{
	auto* settings = new JPH::CharacterVirtualSettings();

	settings->mUp = JPH::Vec3::sAxisY();
	settings->mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);

	const float halfCylinderHeight = std::max(0.0f, 0.5f * (playerHeight - 2.0f * playerRadius));
	settings->mShape = new JPH::CapsuleShape(halfCylinderHeight, playerRadius);

	playerCharacter = new JPH::CharacterVirtual(
		settings,
		JPH::RVec3(player.m_pos.x, player.m_pos.y + playerHeight * 0.5f, player.m_pos.z),
		JPH::Quat::sIdentity(),
		physics->GetPhysicsSystem()
	);
}

void Game::SyncPlayerFromVirtual()
{
	JPH::RVec3 pos = playerCharacter->GetPosition();
	JPH::Vec3 vel = playerCharacter->GetLinearVelocity();

	player.m_pos = {
		(float)pos.GetX(),
		(float)(pos.GetY() - playerHeight * 0.5f),
		(float)pos.GetZ()
	};

	player.m_velocity = {
		vel.GetX(),
		vel.GetY(),
		vel.GetZ()
	};

	player.m_isGrounded =
		(playerCharacter->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround);
}

void Game::SetVirtualPlayerFootPosition(Vector3 footPosition)
{
	if (!playerCharacter)
		return;

	player.m_pos = footPosition;

	playerCharacter->SetPosition(
		JPH::RVec3(
			footPosition.x,
			footPosition.y + playerHeight * 0.5f,
			footPosition.z
		)
	);
}

void Game::ResolvePlayerCustomerSlide()
{
	if (!playerCharacter)
		return;

	const float customerRadius = 0.28f;
	const float skin = 0.03f;

	JPH::Vec3 joltVel = playerCharacter->GetLinearVelocity();

	Vector3 playerPos = player.m_pos;

	for (int iteration = 0; iteration < 2; iteration++)
	{
		bool changed = false;

		for (const Customer& customer : customers)
		{
			if (customer.pendingDespawn)
				continue;

			Vector3 delta = {
				playerPos.x - customer.position.x,
				0.0f,
				playerPos.z - customer.position.z
			};

			float distSq = delta.x * delta.x + delta.z * delta.z;

			float minDist = playerRadius + customerRadius + skin;
			float minDistSq = minDist * minDist;

			if (distSq >= minDistSq)
				continue;

			float dist = sqrtf(fmaxf(distSq, 0.000001f));

			Vector3 normal{};

			if (dist > 0.0001f)
			{
				normal = {
					delta.x / dist,
					0.0f,
					delta.z / dist
				};
			}
			else
			{
				normal = {
					sinf(player.yaw),
					0.0f,
					cosf(player.yaw)
				};
			}

			float penetration = minDist - dist;

			playerPos.x += normal.x * penetration;
			playerPos.z += normal.z * penetration;

			// Remove only the velocity component pushing into the customer.
			// Keep tangent velocity so the player can slide around the customer.
			float vx = joltVel.GetX();
			float vz = joltVel.GetZ();

			float into = vx * normal.x + vz * normal.z;

			if (into < 0.0f)
			{
				vx -= normal.x * into;
				vz -= normal.z * into;

				joltVel.SetX(vx);
				joltVel.SetZ(vz);
			}

			changed = true;
		}

		if (!changed)
			break;
	}

	SetVirtualPlayerFootPosition(playerPos);
	playerCharacter->SetLinearVelocity(joltVel);

	player.m_velocity = {
		joltVel.GetX(),
		joltVel.GetY(),
		joltVel.GetZ()
	};
}

void Game::UpdateVirtualPlayer(float dt)
{
	if (!playerCharacter || !physics)
		return;
	player.input();

	Vector2 inputVec = { (float)player.m_sideway, (float)-player.m_forward };

#if NORMALIZE_INPUT
	if ((player.m_sideway != 0) && (player.m_forward != 0))
		inputVec = Vector2Normalize(inputVec);
#endif

	Vector3 front = { sinf(player.yaw), 0.0f, cosf(player.yaw) };
	Vector3 right = { cosf(-player.yaw), 0.0f, sinf(-player.yaw) };

	Vector3 desiredDir = {
		inputVec.x * right.x + inputVec.y * front.x,
		0.0f,
		inputVec.x * right.z + inputVec.y * front.z
	};

	player.m_dir = Vector3Lerp(player.m_dir, desiredDir, CONTROL * dt);

	const float maxSpeed = player.m_crouching ? CROUCH_SPEED : MAX_SPEED;

	JPH::Vec3 velocity(
		player.m_dir.x * maxSpeed,
		playerCharacter->GetLinearVelocity().GetY(),
		player.m_dir.z * maxSpeed
	);

	if (player.m_jumpPressed &&
		playerCharacter->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround)
	{
		velocity.SetY(JUMP_FORCE);
	}

	playerCharacter->SetLinearVelocity(velocity);

	physics->UpdateCharacterVirtual(*playerCharacter, dt, JPH::Vec3(0.0f, -GRAVITY, 0.0f));

	SyncPlayerFromVirtual();
}

void Game::AccumulateScenePropSubtreeBoundsLocal(
	int propIndex,
	Matrix rootInverse,
	BoundingBox& outBounds,
	bool& hasBounds
) const
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return;

	const SceneProp& prop = sceneProps[propIndex];

	if (prop.model != nullptr)
	{
		BoundingBox modelBounds = GetModelBoundingBox(*prop.model);
		Matrix drawMatrix = GetScenePropDrawMatrix(prop);

		Vector3 corners[8] =
		{
			{ modelBounds.min.x, modelBounds.min.y, modelBounds.min.z },
			{ modelBounds.max.x, modelBounds.min.y, modelBounds.min.z },
			{ modelBounds.min.x, modelBounds.max.y, modelBounds.min.z },
			{ modelBounds.max.x, modelBounds.max.y, modelBounds.min.z },

			{ modelBounds.min.x, modelBounds.min.y, modelBounds.max.z },
			{ modelBounds.max.x, modelBounds.min.y, modelBounds.max.z },
			{ modelBounds.min.x, modelBounds.max.y, modelBounds.max.z },
			{ modelBounds.max.x, modelBounds.max.y, modelBounds.max.z }
		};

		for (int i = 0; i < 8; i++)
		{
			Vector3 worldPoint = Vector3Transform(corners[i], drawMatrix);
			Vector3 rootLocalPoint = Vector3Transform(worldPoint, rootInverse);

			ExpandBoundsWithPoint(outBounds, rootLocalPoint, hasBounds);
		}
	}

	for (int childIndex : prop.childIndices)
	{
		AccumulateScenePropSubtreeBoundsLocal(
			childIndex,
			rootInverse,
			outBounds,
			hasBounds
		);
	}
}

bool Game::RebuildScenePropSubtreeCollider(int propIndex)
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return false;

	UpdateScenePropWorldTransforms();

	SceneProp& root = sceneProps[propIndex];

	Matrix rootInverse = MatrixInvert(GetScenePropColliderBaseMatrix(root));

	BoundingBox localBounds{};
	bool hasBounds = false;

	AccumulateScenePropSubtreeBoundsLocal(
		propIndex,
		rootInverse,
		localBounds,
		hasBounds
	);

	if (!hasBounds)
	{
		TraceLog(
			LOG_WARNING,
			"Cannot rebuild subtree collider. No renderable child meshes found for: %s",
			root.name.c_str()
		);

		return false;
	}

	Vector3 size = {
		localBounds.max.x - localBounds.min.x,
		localBounds.max.y - localBounds.min.y,
		localBounds.max.z - localBounds.min.z
	};

	Vector3 center = {
		(localBounds.min.x + localBounds.max.x) * 0.5f,
		(localBounds.min.y + localBounds.max.y) * 0.5f,
		(localBounds.min.z + localBounds.max.z) * 0.5f
	};

	const float minSize = 0.01f;

	root.colliderSize = {
		fmaxf(size.x, minSize),
		fmaxf(size.y, minSize),
		fmaxf(size.z, minSize)
	};

	root.colliderOffset = center;

	Vector3 scaledColliderAbs = {
		fabsf(root.colliderSize.x * root.scale.x),
		fabsf(root.colliderSize.y * root.scale.y),
		fabsf(root.colliderSize.z * root.scale.z)
	};

	Vector3 halfExtents = {
		scaledColliderAbs.x * 0.5f,
		scaledColliderAbs.y * 0.5f,
		scaledColliderAbs.z * 0.5f
	};

	if (!IsUsableJoltHalfExtents(halfExtents))
	{
		TraceLog(
			LOG_WARNING,
			"Subtree collider invalid for %s: size=(%.3f %.3f %.3f), half=(%.3f %.3f %.3f)",
			root.name.c_str(),
			root.colliderSize.x,
			root.colliderSize.y,
			root.colliderSize.z,
			halfExtents.x,
			halfExtents.y,
			halfExtents.z
		);

		return false;
	}

	TraceLog(
		LOG_INFO,
		"Rebuilt subtree collider for %s: offset=(%.3f %.3f %.3f), size=(%.3f %.3f %.3f)",
		root.name.c_str(),
		root.colliderOffset.x,
		root.colliderOffset.y,
		root.colliderOffset.z,
		root.colliderSize.x,
		root.colliderSize.y,
		root.colliderSize.z
	);

	return true;
}

std::vector<Game::ScenePropState> Game::CaptureScenePropStates() const
{
	std::vector<ScenePropState> states;
	states.reserve(sceneProps.size());

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		const SceneProp& p = sceneProps[i];

		ScenePropState s{};
		s.key = GetScenePropStateKey(p, i);
		s.name = p.name;

		s.sourceGlbPath = p.sourceGlbPath;
		s.sourceNodeName = p.sourceNodeName;
		s.sourceNodeIndex = p.sourceNodeIndex;
		s.importedFromGlbScene = p.importedFromGlbScene;

		s.importedEditorOffset = p.importedEditorOffset;
		s.importedEditorRotationDeg = p.importedEditorRotationDeg;
		s.importedEditorScale = p.importedEditorScale;
		s.itemTag = p.itemTag;
		s.pickupCategory = p.pickupCategory;
		s.tradeItemId = p.tradeItemId;

		s.inspectDialogueTag = p.inspectDialogueTag;
		s.inspectDialogueLines = p.inspectDialogueLines;

		s.currentPlacementSpotIndex = p.currentPlacementSpotIndex;
		s.homePlacementSpotIndex = p.homePlacementSpotIndex;
		s.preferHomePlacement = p.preferHomePlacement;
		s.placedByCustomer = p.placedByCustomer;
		s.manualColliderOverride = p.manualColliderOverride;

		s.parentIndex = p.parentIndex;

		s.position = p.position;
		s.rotationDeg = p.rotationDeg;
		s.scale = p.scale;

		s.localPosition = p.localPosition;
		s.localRotationDeg = p.localRotationDeg;
		s.localScale = p.localScale;

		s.lockUniformScale = p.lockUniformScale;

		s.visible = p.visible;
		s.hasCollision = p.hasCollision;
		s.blocksPlayer = p.blocksPlayer;

		s.useNormalCollision = p.useNormalCollision;
		s.useJoltCollider = p.useJoltCollider;

		s.simulatePhysics = p.simulatePhysics;
		s.syncFromPhysics = p.syncFromPhysics;
		s.editLockPhysics = p.editLockPhysics;

		s.colliderOffset = p.colliderOffset;
		s.colliderSize = p.colliderSize;

		s.holdOffsetLocal = p.holdOffsetLocal;
		s.holdRotationOffsetDeg = p.holdRotationOffsetDeg;
		s.holdFollowCameraPitch = p.holdFollowCameraPitch;
		s.snapUprightOnDrop = p.snapUprightOnDrop;
		s.dropRotationOffsetDeg = p.dropRotationOffsetDeg;

		s.texUsage = p.texUsage;
		s.transparentMaterialIndices = p.transparentMaterialIndices;
		s.transparentAlpha = p.transparentAlpha;

		states.push_back(s);
	}

	return states;
}

static float GetPropApproxRadius(Vector3 size)
{
	return 0.5f * sqrtf(size.x * size.x + size.y * size.y + size.z * size.z);
}

bool Game::IsSphereInCameraView(
	const Camera3D& cam,
	Vector3 center,
	float radius,
	float maxDistance
) const
{
	Vector3 toCenter = Vector3Subtract(center, cam.position);
	float dist = Vector3Length(toCenter);

	if (dist > maxDistance + radius)
		return false;

	if (dist < 0.001f)
		return true;

	Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
	Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, cam.up));
	Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));

	float z = Vector3DotProduct(toCenter, forward);
	float x = Vector3DotProduct(toCenter, right);
	float y = Vector3DotProduct(toCenter, up);

	// Behind camera
	if (z < -radius)
		return false;

	float aspect = (float)GetScreenWidth() / (float)GetScreenHeight();

	float vHalf = cam.fovy * DEG2RAD * 0.5f;
	float hHalf = atanf(tanf(vHalf) * aspect);

	float maxX = tanf(hHalf) * z + radius;
	float maxY = tanf(vHalf) * z + radius;

	if (fabsf(x) > maxX)
		return false;

	if (fabsf(y) > maxY)
		return false;

	return true;
}

void Game::ApplyScenePropStates(const std::vector<ScenePropState>& states)
{
	std::unordered_map<std::string, int> indexByKey;

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		indexByKey[GetScenePropStateKey(sceneProps[i], i)] = i;
	}

	for (int i = 0; i < (int)states.size(); i++)
	{
		const ScenePropState& s = states[i];

		bool savedRuntimeCustomerItem =
			s.name.rfind("Customer Item - ", 0) == 0 ||
			s.placedByCustomer;

		if (savedRuntimeCustomerItem)
		{
			continue;
		}

		int propIndex = -1;

		auto found = indexByKey.find(s.key);
		if (found != indexByKey.end())
		{
			propIndex = found->second;
		}
		else
		{
			// Backward compatibility for old save keys:
			// GLB|path|nodeName|index
			if (s.importedFromGlbScene && !s.sourceNodeName.empty())
			{
				std::string legacyIndependentKey = "GLB_NODE|" + s.sourceNodeName;
				auto found2 = indexByKey.find(legacyIndependentKey);

				if (found2 != indexByKey.end())
				{
					propIndex = found2->second;
				}
			}

			// Only manual props may fall back by index.
			// Imported GLB props must not, because GLB order changes easily.

		}

		if (propIndex < 0 || propIndex >= (int)sceneProps.size())
			continue;

		SceneProp& p = sceneProps[propIndex];

		p.name = s.name;

		p.position = s.position;
		p.rotationDeg = s.rotationDeg;
		p.scale = s.scale;

		p.localPosition = s.localPosition;
		p.localRotationDeg = s.localRotationDeg;
		p.localScale = s.localScale;

		p.importedEditorOffset = s.importedEditorOffset;
		p.importedEditorRotationDeg = s.importedEditorRotationDeg;
		p.importedEditorScale = s.importedEditorScale;
		p.itemTag = s.itemTag;
		p.pickupCategory = s.pickupCategory;
		p.tradeItemId = s.tradeItemId;

		p.inspectDialogueTag = s.inspectDialogueTag;
		p.inspectDialogueLines = s.inspectDialogueLines;

		p.currentPlacementSpotIndex = s.currentPlacementSpotIndex;
		p.homePlacementSpotIndex = s.homePlacementSpotIndex;
		p.preferHomePlacement = s.preferHomePlacement;
		p.placedByCustomer = s.placedByCustomer;
		p.manualColliderOverride = s.manualColliderOverride;

		p.lockUniformScale = s.lockUniformScale;

		p.visible = s.visible;
		p.hasCollision = s.hasCollision;
		p.blocksPlayer = s.blocksPlayer;

		p.useNormalCollision = s.useNormalCollision;
		p.useJoltCollider = s.useJoltCollider;

		p.simulatePhysics = s.simulatePhysics;
		p.syncFromPhysics = s.syncFromPhysics;
		p.editLockPhysics = s.editLockPhysics;

		p.colliderOffset = s.colliderOffset;
		p.colliderSize = s.colliderSize;

		p.holdOffsetLocal = s.holdOffsetLocal;
		p.holdRotationOffsetDeg = s.holdRotationOffsetDeg;
		p.holdFollowCameraPitch = s.holdFollowCameraPitch;
		p.snapUprightOnDrop = s.snapUprightOnDrop;
		p.dropRotationOffsetDeg = s.dropRotationOffsetDeg;

		p.texUsage = s.texUsage;
		p.transparentMaterialIndices = s.transparentMaterialIndices;
		p.transparentAlpha = s.transparentAlpha;
	}

	UpdateScenePropWorldTransforms();

	// Imported renderable GLB props use visual offset instead of normal local transform.
	// Re-apply their runtime transform after world transforms are rebuilt.
	for (SceneProp& prop : sceneProps)
	{
		if (prop.importedFromGlbScene && prop.model != nullptr)
		{
			ApplyImportedEditorTransformToRuntime(prop);
		}
	}

	ApplyAllScenePropTransformsToBodies();

	customerNavGridDirty = true;
	RebuildItemPlacementSpotOccupancy();
	MarkShadowMapsDirty();
}
void Game::RestoreCorruptedImportedScenePropNames()
{
	int restored = 0;

	for (SceneProp& prop : sceneProps)
	{
		if (!prop.importedFromGlbScene)
			continue;

		if (prop.sourceNodeName.empty())
			continue;

		bool looksCorrupted =
			prop.name.rfind("Customer Item - ", 0) == 0;

		if (!looksCorrupted)
			continue;

		prop.name = prop.sourceNodeName;

		prop.placedByCustomer = false;
		prop.owningCustomerIndex = -1;
		prop.scannedForCustomer = false;
		prop.scannedPriceYen = 0;

		prop.currentPlacementSpotIndex = -1;
		prop.homePlacementSpotIndex = -1;

		prop.canPickup = false;
		prop.simulatePhysics = false;
		prop.syncFromPhysics = false;
		prop.editLockPhysics = true;

		prop.tradeItemId.clear();
		prop.itemTag = "generic";

		restored++;
	}

	RebuildItemPlacementSpotOccupancy();
	RepairScenePropHierarchyLinks();

	TraceLog(LOG_INFO, "Restored %i corrupted imported scene prop names.", restored);
}
void Game::ApplyAllScenePropTransformsToBodies()
{
	for (SceneProp& prop : sceneProps)
	{
		if (!prop.bodyId.IsInvalid())
		{
			ApplyScenePropTransformToBody(prop);
		}
	}
}

std::vector<Game::CustomerState> Game::CaptureCustomerStates() const
{
	std::vector<CustomerState> states;
	states.reserve(customers.size());

	for (int i = 0; i < (int)customers.size(); i++)
	{
		const Customer& c = customers[i];

		CustomerState s{};
		s.index = i;

		s.position = c.position;
		s.targetPosition = c.targetPosition;
		s.yawDeg = c.yawDeg;
		s.scale = c.scale;

		s.moveSpeed = c.moveSpeed;

		s.usePOINavigation = c.usePOINavigation;
		s.editorFrozen = c.editorFrozen;

		s.currentPOIIndex = c.currentPOIIndex;
		s.targetPOIIndex = c.targetPOIIndex;
		s.destinationPOIIndex = c.destinationPOIIndex;

		s.dialogueScriptId = c.dialogueScriptId;
		s.poiGroup = c.poiGroup;

		states.push_back(s);
	}

	return states;
}
void Game::ApplyCustomerStates(const std::vector<CustomerState>& states)
{
	for (const CustomerState& s : states)
	{
		if (s.index < 0 || s.index >= (int)customers.size())
			continue;

		Customer& c = customers[s.index];

		c.position = s.position;
		c.targetPosition = s.targetPosition;
		c.yawDeg = s.yawDeg;
		c.scale = s.scale;

		c.moveSpeed = s.moveSpeed;

		c.usePOINavigation = s.usePOINavigation;
		c.editorFrozen = s.editorFrozen;

		c.currentPOIIndex = s.currentPOIIndex;
		c.targetPOIIndex = s.targetPOIIndex;
		c.destinationPOIIndex = s.destinationPOIIndex;

		c.dialogueScriptId = s.dialogueScriptId;
		c.poiGroup = s.poiGroup;

		c.hasMoveTarget = false;
		c.pathWaypoints.clear();
		c.pathWaypointCursor = 0;
		c.movementPauseTimer = 0.0f;
		c.repathTimer = 0.0f;

		c.SetAnimState(CustomerAnimState::Idle);

		ApplyCustomerEditorTransform(s.index);

		bool isSeller =
			c.poiGroup == "seller" ||
			c.dialogueScriptId.rfind("seller_", 0) == 0;

		c.role = isSeller
			? CustomerRole::Seller
			: CustomerRole::Browser;

		c.aiState = isSeller
			? CustomerAIState::SellerGoingToQueue
			: CustomerAIState::BrowserBrowsing;

		c.assignedItemPOIIndex = -1;
		c.assignedQueueSlotIndex = -1;
		c.hasPickedItem = false;
		c.pendingDespawn = false;

		c.currentPOIIndex = -1;
		c.targetPOIIndex = -1;
		c.destinationPOIIndex = -1;

		c.hasMoveTarget = false;
		c.pathWaypoints.clear();
		c.pathWaypointCursor = 0;

		c.poiWaitTimer = 0.25f + 0.2f * (float)(s.index);
		c.movementPauseTimer = 0.0f;

	}

	customerNavGridDirty = true;
	MarkShadowMapsDirty();
}
Game::PlacementHit Game::FindPlacementSurfaceRaycast(
	Vector3 rayOrigin,
	Vector3 rayDir,
	float maxDistance,
	int ignoreScenePropIndex
) const
{
	PlacementHit best{};

	// scene props
	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		if (i == ignoreScenePropIndex) continue;

		const SceneProp& prop = sceneProps[i];
		if (!prop.visible || !prop.hasCollision) continue;

		Vector3 size = {
			prop.colliderSize.x * prop.scale.x,
			prop.colliderSize.y * prop.scale.y,
			prop.colliderSize.z * prop.scale.z
		};

		Quaternion rot = QuaternionFromEuler(
			prop.rotationDeg.x * DEG2RAD,
			prop.rotationDeg.y * DEG2RAD,
			prop.rotationDeg.z * DEG2RAD
		);

		Vector3 center = Vector3Add(prop.position, GetScenePropRotatedOffset(prop));

		float hitDistance = 0.0f;
		if (RaycastAgainstOBB(rayOrigin, rayDir, center, rot, size, maxDistance, hitDistance))
		{
			if (!best.valid || hitDistance < best.distance)
			{
				best.valid = true;
				best.distance = hitDistance;
				best.point = Vector3Add(rayOrigin, Vector3Scale(rayDir, hitDistance));
			}
		}
	}

	// blockout boxes
	for (const BlockoutBox& box : blockoutBoxes)
	{
		if (!box.visible || !box.hasCollision) continue;

		Quaternion rot = QuaternionFromEuler(
			box.rotationDeg.x * DEG2RAD,
			box.rotationDeg.y * DEG2RAD,
			box.rotationDeg.z * DEG2RAD
		);

		float hitDistance = 0.0f;
		if (RaycastAgainstOBB(rayOrigin, rayDir, box.position, rot, box.size, maxDistance, hitDistance))
		{
			if (!best.valid || hitDistance < best.distance)
			{
				best.valid = true;
				best.distance = hitDistance;
				best.point = Vector3Add(rayOrigin, Vector3Scale(rayDir, hitDistance));
			}
		}
	}

	return best;
}
Game::PlacementHit Game::FindDownwardPlacementRaycast(
	Vector3 worldPos,
	float probeHeight,
	int ignoreScenePropIndex
) const
{
	Vector3 rayOrigin = Vector3Add(worldPos, { 0.0f, probeHeight, 0.0f });
	Vector3 rayDir = { 0.0f, -1.0f, 0.0f };

	return FindPlacementSurfaceRaycast(rayOrigin, rayDir, probeHeight * 2.0f, ignoreScenePropIndex);
}
void Game::AddSceneProp(
	const std::string& name,
	Model* model,
	Vector3 position,
	Vector3 rotationDeg,
	Vector3 scale,
	bool hasCollision,
	Vector3 colliderSize,
	Vector3 colliderOffset,
	bool useJoltCollider,
	bool useNormalCollision,
	Color tint
)
{
	SceneProp prop{};

	prop.name = name;
	prop.model = model;

	prop.position = position;
	prop.rotationDeg = rotationDeg;
	prop.scale = scale;
	prop.castsShadow = ShouldScenePropCastShadowByName(name);

	prop.localPosition = position;
	prop.localRotationDeg = rotationDeg;
	prop.localScale = scale;
	prop.worldMatrix = BuildTRSMatrix(position, rotationDeg, scale);

	prop.parentIndex = -1;
	prop.childIndices.clear();

	prop.tint = tint;
	prop.visible = true;

	prop.hasCollision = hasCollision;
	prop.blocksPlayer = hasCollision;
	prop.useJoltCollider = useJoltCollider && hasCollision;
	prop.useNormalCollision = useNormalCollision && hasCollision;

	bool useAutoCollider =
		colliderSize.x <= 0.0f &&
		colliderSize.y <= 0.0f &&
		colliderSize.z <= 0.0f;

	// IMPORTANT: transform-only parent nodes have no model.
	// They must never try to calculate model bounds.
	if (model == nullptr)
	{
		prop.hasCollision = false;
		prop.blocksPlayer = false;
		prop.useJoltCollider = false;
		prop.useNormalCollision = false;

		prop.colliderSize = { 1.0f, 1.0f, 1.0f };
		prop.colliderOffset = { 0.0f, 0.0f, 0.0f };

		sceneProps.emplace_back(prop);
		return;
	}

	if (useAutoCollider)
	{
		BoundingBox bounds = GetModelBoundingBox(*model);

		Vector3 localSize = {
			bounds.max.x - bounds.min.x,
			bounds.max.y - bounds.min.y,
			bounds.max.z - bounds.min.z
		};

		Vector3 localCenter = {
			(bounds.min.x + bounds.max.x) * 0.5f,
			(bounds.min.y + bounds.max.y) * 0.5f,
			(bounds.min.z + bounds.max.z) * 0.5f
		};

		const float minSize = 0.01f;

		prop.colliderSize = {
			fmaxf(localSize.x, minSize),
			fmaxf(localSize.y, minSize),
			fmaxf(localSize.z, minSize)
		};

		prop.colliderOffset = localCenter;
	}
	else
	{
		prop.colliderSize = colliderSize;
		prop.colliderOffset = colliderOffset;
	}

	sceneProps.emplace_back(prop);
}

bool Game::IsImportedScenePropColliderUsable(const SceneProp& prop) const
{
	if (!prop.importedFromGlbScene)
		return true;

	Vector3 scaledColliderAbs = {
		fabsf(prop.colliderSize.x * prop.scale.x),
		fabsf(prop.colliderSize.y * prop.scale.y),
		fabsf(prop.colliderSize.z * prop.scale.z)
	};

	Vector3 halfExtents = {
		scaledColliderAbs.x * 0.5f,
		scaledColliderAbs.y * 0.5f,
		scaledColliderAbs.z * 0.5f
	};

	return IsUsableJoltHalfExtents(halfExtents);
}

void Game::ApplyImportedCollisionNamingRules()
{
	std::function<void(int, bool)> visit;

	visit = [&](int propIndex, bool parentCollisionBranchActive)
		{
			if (propIndex < 0 || propIndex >= (int)sceneProps.size())
				return;

			SceneProp& prop = sceneProps[propIndex];

			bool isCollisionBranch =
				ShouldImportedNodeHaveCollision(prop.name) ||
				ShouldImportedNodeHaveCollision(prop.sourceNodeName);

			bool isAggregateProp =
				ShouldImportedNodeBeAggregateProp(prop.name) ||
				ShouldImportedNodeBeAggregateProp(prop.sourceNodeName);

			bool collisionBranchActive =
				parentCollisionBranchActive ||
				isCollisionBranch;

			if (prop.importedFromGlbScene)
			{
				if (prop.manualColliderOverride || prop.canPickup)
				{
					if (prop.model != nullptr && IsImportedScenePropColliderUsable(prop))
					{
						prop.hasCollision = true;
						prop.blocksPlayer = true;
						prop.useJoltCollider = true;
						prop.useNormalCollision = false;
					}

					for (int childIndex : prop.childIndices)
					{
						visit(childIndex, collisionBranchActive);
					}

					return;
				}

				// Start safe. Nothing imported gets collision unless a rule enables it.
				prop.hasCollision = false;
				prop.blocksPlayer = false;
				prop.useJoltCollider = false;
				prop.useNormalCollision = false;
				prop.bodyId = JPH::BodyID();

				// PROP_ parent: one aggregate collider around the whole subtree.
				if (isAggregateProp)
				{
					bool rebuilt = RebuildScenePropSubtreeCollider(propIndex);

					if (rebuilt && IsImportedScenePropColliderUsable(prop))
					{
						prop.hasCollision = true;
						prop.blocksPlayer = true;
						prop.useJoltCollider = true;
						prop.useNormalCollision = false;
					}
				}
				// COL_ / WALL_ / FLOOR_ / BLOCK_ branch:
				// renderable mesh nodes get individual colliders.
				else if (collisionBranchActive && prop.model != nullptr)
				{
					if (IsImportedScenePropColliderUsable(prop))
					{
						prop.hasCollision = true;
						prop.blocksPlayer = true;
						prop.useJoltCollider = true;
						prop.useNormalCollision = false;
					}
				}
			}

			for (int childIndex : prop.childIndices)
			{
				visit(childIndex, collisionBranchActive);
			}
		};

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		if (sceneProps[i].parentIndex == -1)
		{
			visit(i, false);
		}
	}
}

struct GltfMeshRange
{
	int firstMesh = 0;
	int meshCount = 0;
};

static Matrix CgltfMatrixToRaylibMatrix(const cgltf_float m[16])
{
	Matrix result{};

	result.m0 = (float)m[0];
	result.m1 = (float)m[1];
	result.m2 = (float)m[2];
	result.m3 = (float)m[3];

	result.m4 = (float)m[4];
	result.m5 = (float)m[5];
	result.m6 = (float)m[6];
	result.m7 = (float)m[7];

	result.m8 = (float)m[8];
	result.m9 = (float)m[9];
	result.m10 = (float)m[10];
	result.m11 = (float)m[11];

	result.m12 = (float)m[12];
	result.m13 = (float)m[13];
	result.m14 = (float)m[14];
	result.m15 = (float)m[15];

	return result;
}

static Vector3 QuaternionToEulerDegImported(Quaternion q)
{
	Vector3 euler = QuaternionToEuler(q);

	return {
		euler.x * RAD2DEG,
		euler.y * RAD2DEG,
		euler.z * RAD2DEG
	};
}

void Game::LoadJapanStoreStatic()
{
	japanStoreStaticModel = LoadModel("Models/JapanStore2.glb");
	japanStoreStaticLoaded = japanStoreStaticModel.meshCount > 0;

	if (!japanStoreStaticLoaded)
	{
		TraceLog(LOG_WARNING, "Failed to load JapanStore.glb");
		return;
	}
	/*
		for (int i = 0; i < japanStoreStaticModel.materialCount; i++)
	{
		japanStoreStaticModel.materials[i].shader = pbrShader;
	}

	ApplyEnvironmentCubemap(japanStoreStaticModel);
	AttachShadowTextureToModel(japanStoreStaticModel, shadowCasters[0].shadowMap.texture);
	*/


	int totalVertices = 0;
	int totalTriangles = 0;

	for (int i = 0; i < japanStoreStaticModel.meshCount; i++)
	{
		totalVertices += japanStoreStaticModel.meshes[i].vertexCount;
		totalTriangles += japanStoreStaticModel.meshes[i].triangleCount;
	}

	TraceLog(
		LOG_INFO,
		"JapanStore static stats: meshes=%i materials=%i vertices=%i triangles=%i",
		japanStoreStaticModel.meshCount,
		japanStoreStaticModel.materialCount,
		totalVertices,
		totalTriangles
	);

}

void Game::DrawJapanStoreStatic() const
{
	if (!japanStoreStaticLoaded)
		return;

	DrawModelEx(
		japanStoreStaticModel,
		{ 0.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f },
		0.0f,
		{ 1.0f, 1.0f, 1.0f },
		WHITE
	);
}

void Game::BuildSceneProps()
{
	LoadingPulse("Clearing imported scene props...", 0.58f, true);

	sceneProps.clear();

	ClearImportedGlbScene();

	LoadingPulse("Importing JapanStore scene...", 0.60f, true);

	ImportGlbSceneAsProps("Models/JapanStore20_1k.glb");

	LoadingPulse("Creating pickup props...", 0.72f, true);

	AddSceneProp(
		"GameBoy",
		&gBoy,
		{ 0.5f, 0.0f, -1.5f },
		{ 0.0f, 30.0f, 0.0f },
		{ 1.0f, 1.0f, 1.0f },
		true,
		{ 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f },
		true,
		false,
		WHITE
	);

	sceneProps.back().canPickup = true;
	sceneProps.back().holdOffsetLocal = { 0.0f, -0.15f, -0.15f };
	sceneProps.back().holdRotationOffsetDeg = { 0.0f, 90.0f, 0.0f };
	sceneProps.back().holdFollowCameraPitch = true;

	sceneProps.back().pickupCategory = PickupItemCategory::Selling;
	sceneProps.back().tradeItemId = "gameboy_console";
	sceneProps.back().itemTag = "retro_console";
	sceneProps.back().inspectDialogueTag = "gameboy_console";

	sceneProps.back().snapUprightOnDrop = true;
	sceneProps.back().dropRotationOffsetDeg = { 0.0f, -90.0f, 0.0f };

	sceneProps.back().simulatePhysics = true;
	sceneProps.back().syncFromPhysics = false;
	sceneProps.back().editLockPhysics = true;
	sceneProps.back().useJoltCollider = true;
	sceneProps.back().useNormalCollision = false;
	sceneProps.back().lockUniformScale = true;

	LoadingPulse("Scene props created.", 0.74f, true);
}

void Game::ClearImportedGlbScene()
{
	// Model views borrow mesh/material memory from the master models.
	// Do not call UnloadModel() on these views.
	importedGlbModelViews.clear();

	for (auto& master : importedGlbMasters)
	{
		if (master && master->model.meshCount > 0)
		{
			UnloadModel(master->model);
			master->model = {};
		}
	}

	importedGlbMasters.clear();
}

Model* Game::CreateImportedModelView(Model& master, int firstMesh, int meshCount)
{
	if (firstMesh < 0 || meshCount <= 0)
		return nullptr;

	if (firstMesh + meshCount > master.meshCount)
		return nullptr;

	auto view = std::make_unique<Model>();

	// Clear the model first.
	*view = {};

	view->transform = MatrixIdentity();

	view->meshCount = meshCount;
	view->materialCount = master.materialCount;

	// Borrowed pointers from the master GLB model.
	// The master model owns this memory.
	view->meshes = master.meshes + firstMesh;
	view->materials = master.materials;
	view->meshMaterial = master.meshMaterial + firstMesh;

	importedGlbModelViews.push_back(std::move(view));
	return importedGlbModelViews.back().get();
}

Matrix Game::BuildTRSMatrix(Vector3 pos, Vector3 rotDeg, Vector3 scale) const
{
	Quaternion q = QuaternionFromEuler(
		rotDeg.x * DEG2RAD,
		rotDeg.y * DEG2RAD,
		rotDeg.z * DEG2RAD
	);

	Matrix s = MatrixScale(scale.x, scale.y, scale.z);
	Matrix r = QuaternionToMatrix(q);
	Matrix t = MatrixTranslate(pos.x, pos.y, pos.z);

	return MatrixMultiply(MatrixMultiply(s, r), t);
}
void Game::UpdateScenePropWorldTransforms()
{
	for (SceneProp& prop : sceneProps)
	{
		prop.worldMatrix = MatrixIdentity();
	}

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		if (sceneProps[i].parentIndex == -1)
		{
			UpdateScenePropWorldTransformRecursive(i, MatrixIdentity());
		}
	}
}
void Game::UpdateScenePropWorldTransformRecursive(int propIndex, Matrix parentMatrix)
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return;

	SceneProp& prop = sceneProps[propIndex];

	prop.localScale.x = SafeNonZeroScale(prop.localScale.x);
	prop.localScale.y = SafeNonZeroScale(prop.localScale.y);
	prop.localScale.z = SafeNonZeroScale(prop.localScale.z);

	Matrix local = BuildTRSMatrix(
		prop.localPosition,
		prop.localRotationDeg,
		prop.localScale
	);

	prop.worldMatrix = MatrixMultiply(local, parentMatrix);

	Vector3 worldPos{};
	Quaternion worldRot{};
	Vector3 worldScale{};

	MatrixDecompose(prop.worldMatrix, &worldPos, &worldRot, &worldScale);

	prop.position = worldPos;
	prop.rotationDeg = {
		QuaternionToEuler(worldRot).x * RAD2DEG,
		QuaternionToEuler(worldRot).y * RAD2DEG,
		QuaternionToEuler(worldRot).z * RAD2DEG
	};
	prop.scale = worldScale;

	prop.scale.x = SafeNonZeroScale(prop.scale.x);
	prop.scale.y = SafeNonZeroScale(prop.scale.y);
	prop.scale.z = SafeNonZeroScale(prop.scale.z);

	for (int childIndex : prop.childIndices)
	{
		UpdateScenePropWorldTransformRecursive(childIndex, prop.worldMatrix);
	}
}

void Game::ImportGlbSceneAsProps(const char* glbPath)
{
	double importStart = GetTime();
	LoadingPulse("Parsing imported GLB scene...", 0.61f, true);

	cgltf_options options{};
	cgltf_data* data = nullptr;

	cgltf_result result = cgltf_parse_file(&options, glbPath, &data);


	if (result != cgltf_result_success || data == nullptr)
	{
		TraceLog(LOG_WARNING, "Failed to parse GLB scene: %s", glbPath);
		return;
	}
	/*
		LoadingPulse("Loading imported GLB buffers...", 0.62f, true);

	result = cgltf_load_buffers(&options, data, glbPath);

	if (result != cgltf_result_success)
	{
		TraceLog(LOG_WARNING, "Failed to load GLB buffers: %s", glbPath);
		cgltf_free(data);
		return;
	}
	*/

	LoadingPulse("Loading imported GLB model...", 0.63f, true);
	auto master = std::make_unique<ImportedGlbMasterModel>();
	master->path = glbPath;
	master->model = LoadModel(glbPath);
	LoadingPulse("Loading imported GLB model...", 0.63f, true);
	if (master->model.meshCount <= 0)
	{
		TraceLog(LOG_WARNING, "raylib failed to load GLB model: %s", glbPath);
		cgltf_free(data);
		return;
	}

	std::vector<int> importedAlphaModes(master->model.materialCount, 0);
	std::vector<float> importedAlphaCutoffs(master->model.materialCount, 0.5f);
	std::vector<float> importedReflectionStrengths(master->model.materialCount, 0.025f);

	int raylibMaterialOffset = 0;

	std::vector<ImportedMaterialRenderSettings> gltfMaterialSettings(
		data->materials_count
	);

	for (cgltf_size i = 0; i < data->materials_count; i++)
	{
		if (i % 8 == 0)
		{
			float t = data->materials_count > 0
				? (float)i / (float)data->materials_count
				: 1.0f;

			LoadingPulse(
				TextFormat("Processing imported materials... %i/%i",
					(int)i,
					(int)data->materials_count),
				Lerp(0.66f, 0.68f, t),
				false
			);
		}

		const cgltf_material& gltfMat = data->materials[i];

		int raylibMatIndex = (int)i + raylibMaterialOffset;

		if (raylibMatIndex < 0 || raylibMatIndex >= master->model.materialCount)
			continue;

		int alphaMode = 0; // 0 opaque, 1 mask, 2 blend

		if (gltfMat.alpha_mode == cgltf_alpha_mode_mask)
		{
			alphaMode = 1;
		}
		else if (gltfMat.alpha_mode == cgltf_alpha_mode_blend)
		{
			alphaMode = 2;
		}

		// Blender Principled BSDF Alpha / base color alpha.
		// This catches materials where Alpha Factor is 0.1, 0.5, etc.
		float baseAlpha = 1.0f;

		if (gltfMat.has_pbr_metallic_roughness)
		{
			baseAlpha = (float)gltfMat.pbr_metallic_roughness.base_color_factor[3];
		}

		bool alphaFactorTransparent = baseAlpha < 0.98f;

		if (alphaFactorTransparent && alphaMode == 0)
		{
			alphaMode = 2;
		}

		float importedReflection = 0.025f;

		// Transparent materials such as glass / plexiglass should reflect a bit,
		// but not behave like mirrors.
		if (alphaMode == 2)
		{
			importedReflection = 0.08f;
		}

		// Optional name-based fallback for materials exported as OPAQUE
		// even though they are intended to be glass.
		std::string matName = "";

		if (gltfMat.name != nullptr)
		{
			matName = gltfMat.name;
		}

		std::string lowerName = matName;
		std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

		bool nameLooksTransparent =
			lowerName.find("glass") != std::string::npos ||
			lowerName.find("plexi") != std::string::npos ||
			lowerName.find("acrylic") != std::string::npos ||
			lowerName.find("transparent") != std::string::npos;

		if (nameLooksTransparent)
		{
			alphaMode = 2;
			importedReflection = 0.08f;
		}

		float alphaCutoff = gltfMat.alpha_cutoff > 0.0f
			? (float)gltfMat.alpha_cutoff
			: 0.5f;

		// Keep old material-index arrays as fallback.
		importedAlphaModes[raylibMatIndex] = alphaMode;
		importedAlphaCutoffs[raylibMatIndex] = alphaCutoff;
		importedReflectionStrengths[raylibMatIndex] = importedReflection;

		// New safer primitive/material setting table.
		// This is used later to assign alpha per mesh primitive.
		ImportedMaterialRenderSettings settings{};
		settings.alphaMode = alphaMode;
		settings.alphaCutoff = alphaCutoff;
		settings.baseAlpha = baseAlpha;
		settings.reflectionStrength = importedReflection;

		gltfMaterialSettings[i] = settings;

		TraceLog(
			LOG_INFO,
			"GLB material alpha: gltfMat=%i raylibMat=%i name='%s' alphaMode=%i baseAlpha=%.3f cutoff=%.2f reflection=%.3f",
			(int)i,
			raylibMatIndex,
			matName.c_str(),
			alphaMode,
			baseAlpha,
			alphaCutoff,
			importedReflection
		);
	}

	// Apply PBR shader + environment cubemap to the imported master model.
	ApplyEnvironmentCubemap(master->model);

	// Safe defaults for imported GLB materials.
	// This prevents missing AO/roughness/metallic values from making the model too dark.
	for (int i = 0; i < master->model.materialCount; i++)
	{
		Material& mat = master->model.materials[i];

		mat.shader = pbrShader;

		// If no AO map exists, AO should be 1, not 0.
		if (mat.maps[MATERIAL_MAP_OCCLUSION].texture.id <= 1)
		{
			mat.maps[MATERIAL_MAP_OCCLUSION].value = 1.0f;
		}

		// If no roughness map exists, use a middle roughness.
		if (mat.maps[MATERIAL_MAP_ROUGHNESS].texture.id <= 1)
		{
			mat.maps[MATERIAL_MAP_ROUGHNESS].value = 0.65f;
		}

		// If no metallic map exists, assume non-metal.
		if (mat.maps[MATERIAL_MAP_METALNESS].texture.id <= 1)
		{
			mat.maps[MATERIAL_MAP_METALNESS].value = 0.0f;
		}

		// Avoid black fallback diffuse color.
		Color& baseColor = mat.maps[MATERIAL_MAP_ALBEDO].color;

		if (baseColor.r == 0 && baseColor.g == 0 && baseColor.b == 0)
		{
			baseColor = WHITE;
		}
	}

	Model& masterModel = master->model;
	importedGlbMasters.push_back(std::move(master));

	/*
		std::unordered_map<const cgltf_mesh*, GltfMeshRange> meshRanges;

	int raylibMeshCursor = 0;

	for (cgltf_size i = 0; i < data->meshes_count; i++)
	{
		const cgltf_mesh* mesh = &data->meshes[i];

		GltfMeshRange range{};
		range.firstMesh = raylibMeshCursor;
		range.meshCount = (int)mesh->primitives_count;

		meshRanges[mesh] = range;
		raylibMeshCursor += range.meshCount;
	}
	*/
	int raylibMeshCursor = 0;

	if (raylibMeshCursor > masterModel.meshCount)
	{
		TraceLog(
			LOG_WARNING,
			"GLB primitive count does not match raylib mesh count. GLB primitives=%i raylib meshes=%i",
			raylibMeshCursor,
			masterModel.meshCount
		);
	}

	cgltf_scene* scene = data->scene;

	if (scene == nullptr && data->scenes_count > 0)
		scene = &data->scenes[0];

	if (scene == nullptr)
	{
		TraceLog(LOG_WARNING, "GLB has no scene: %s", glbPath);
		cgltf_free(data);
		return;
	}

	int importedCount = 0;

	std::function<void(cgltf_node*, int)> visitNode;

	visitNode = [&](cgltf_node* node, int parentPropIndex)
		{
			if (node == nullptr)
				return;

			// ------------------------------------------------------------
			// 1. Read local transform
			// ------------------------------------------------------------
			cgltf_float localMatrixRaw[16];
			cgltf_node_transform_local(node, localMatrixRaw);

			Matrix localMatrix = CgltfMatrixToRaylibMatrix(localMatrixRaw);

			Vector3 localPosition{};
			Quaternion localRotation{};
			Vector3 localScale{};

			MatrixDecompose(localMatrix, &localPosition, &localRotation, &localScale);

			Vector3 localRotationDeg = QuaternionToEulerDegImported(localRotation);

			// ------------------------------------------------------------
			// 2. Read world transform for initial compatibility with your
			// existing AddSceneProp/render/physics code.
			// ------------------------------------------------------------
			cgltf_float worldMatrixRaw[16];
			cgltf_node_transform_world(node, worldMatrixRaw);

			Matrix worldMatrix = CgltfMatrixToRaylibMatrix(worldMatrixRaw);

			Vector3 worldPosition{};
			Quaternion worldRotation{};
			Vector3 worldScale{};

			MatrixDecompose(worldMatrix, &worldPosition, &worldRotation, &worldScale);

			Vector3 worldRotationDeg = QuaternionToEulerDegImported(worldRotation);

			// ------------------------------------------------------------
			// 3. Name
			// ------------------------------------------------------------
			std::string nodeName;

			if (node->name != nullptr && node->name[0] != '\0')
				nodeName = node->name;
			else if (node->mesh != nullptr && node->mesh->name != nullptr && node->mesh->name[0] != '\0')
				nodeName = node->mesh->name;
			else
				nodeName = "ImportedNode_" + std::to_string(importedCount);

			// ------------------------------------------------------------
			// 4. Optional mesh model view
			// ------------------------------------------------------------
			Model* modelView = nullptr;
			int firstMeshForThisNode = -1;
			int meshCountForThisNode = 0;

			if (node->mesh != nullptr)
			{
				meshCountForThisNode = (int)node->mesh->primitives_count;
				firstMeshForThisNode = raylibMeshCursor;

				if (firstMeshForThisNode + meshCountForThisNode <= masterModel.meshCount)
				{
					modelView = CreateImportedModelView(
						masterModel,
						firstMeshForThisNode,
						meshCountForThisNode
					);
				}
				else
				{
					TraceLog(
						LOG_WARNING,
						"GLB node '%s' mesh range out of bounds. first=%i count=%i masterMeshes=%i",
						nodeName.c_str(),
						firstMeshForThisNode,
						meshCountForThisNode,
						masterModel.meshCount
					);
				}

				raylibMeshCursor += meshCountForThisNode;
			}

			// ------------------------------------------------------------
			// 5. Create SceneProp even for transform-only nodes.
			// This gives you Unity-like empty parents.
			// ------------------------------------------------------------
			Vector3 importPosition = worldPosition;
			Vector3 importRotation = worldRotationDeg;
			Vector3 importScale = { 1.0f, 1.0f, 1.0f };

			// Important:
			// If the mesh came from raylib LoadModel(glbPath), do not apply glTF scale again.
			if (modelView != nullptr)
			{
				importScale = { 1.0f, 1.0f, 1.0f };
			}

			AddSceneProp(
				nodeName,
				modelView,
				importPosition,
				importRotation,
				importScale,
				false,
				{ 0.0f, 0.0f, 0.0f },
				{ 0.0f, 0.0f, 0.0f },
				false,
				false,
				WHITE
			);

			SceneProp& prop = sceneProps.back();

			// For now, flatten imported renderable GLB props.
			// This prevents parent scale from being applied again.
			int currentPropIndex = (int)sceneProps.size() - 1;

			if (modelView != nullptr)
			{
				// Flatten renderable imported props.
				// They should not be displayed as children if parentIndex is -1.
				prop.parentIndex = -1;
			}
			else
			{
				// Empty transform nodes can remain in the imported hierarchy.
				prop.parentIndex = parentPropIndex;

				if (parentPropIndex >= 0 && parentPropIndex < (int)sceneProps.size())
				{
					sceneProps[parentPropIndex].childIndices.push_back(currentPropIndex);
				}
			}

			prop.localPosition = localPosition;
			prop.localRotationDeg = localRotationDeg;
			prop.localScale = localScale;
			prop.worldMatrix = worldMatrix;

			prop.importedFromGlbScene = true;
			prop.sourceGlbPath = glbPath;
			prop.sourceNodeName = nodeName;
			prop.sourceNodeIndex = importedCount;

			prop.importBasePosition = importPosition;
			prop.importBaseRotationDeg = importRotation;
			prop.importBaseScale = importScale;

			prop.importedEditorOffset = { 0.0f, 0.0f, 0.0f };
			prop.importedEditorRotationDeg = { 0.0f, 0.0f, 0.0f };
			prop.importedEditorScale = { 1.0f, 1.0f, 1.0f };

			prop.castsShadow =
				ShouldScenePropCastShadowByName(prop.name) &&
				ShouldScenePropCastShadowByName(prop.sourceNodeName);

			prop.canPickup = false;
			prop.lockUniformScale = false;
			prop.useNormalCollision = false;
			prop.simulatePhysics = false;
			prop.syncFromPhysics = false;
			prop.editLockPhysics = true;

			// Texture usage: safer default for imported GLB.
			// Enable normal/MRA later after verifying material map slots.
			prop.texUsage = { 1, 0, 0, 0 };

			prop.materialAlphaModes = importedAlphaModes;
			prop.materialAlphaCutoffs = importedAlphaCutoffs;
			prop.materialReflectionStrengths = importedReflectionStrengths;

			prop.meshAlphaModes.clear();
			prop.meshAlphaCutoffs.clear();
			prop.meshBaseAlphas.clear();
			prop.meshReflectionStrengths.clear();

			if (node->mesh != nullptr && modelView != nullptr)
			{
				cgltf_size primitiveCount = node->mesh->primitives_count;

				prop.meshAlphaModes.resize(primitiveCount, 0);
				prop.meshAlphaCutoffs.resize(primitiveCount, 0.5f);
				prop.meshBaseAlphas.resize(primitiveCount, 1.0f);
				prop.meshReflectionStrengths.resize(primitiveCount, 0.025f);

				for (cgltf_size p = 0; p < primitiveCount; p++)
				{
					const cgltf_primitive& primitive = node->mesh->primitives[p];

					ImportedMaterialRenderSettings settings{};

					if (primitive.material != nullptr && data->materials_count > 0)
					{
						int gltfMaterialIndex = (int)(primitive.material - data->materials);

						if (gltfMaterialIndex >= 0 &&
							gltfMaterialIndex < (int)gltfMaterialSettings.size())
						{
							settings = gltfMaterialSettings[gltfMaterialIndex];
						}
					}

					prop.meshAlphaModes[p] = settings.alphaMode;
					prop.meshAlphaCutoffs[p] = settings.alphaCutoff;
					prop.meshBaseAlphas[p] = settings.baseAlpha;
					prop.meshReflectionStrengths[p] = settings.reflectionStrength;
				}
			}

			// Imported GLB transparency should now come from glTF material alpha,
			// not from the old manual transparentMaterialIndices system.
			prop.transparentMaterialIndices.clear();
			prop.transparentAlpha = 255;

			bool wantsCollision = ShouldImportedNodeHaveCollision(nodeName);
			bool hasRenderableMesh = modelView != nullptr;

			prop.hasCollision = wantsCollision && hasRenderableMesh;
			prop.blocksPlayer = wantsCollision && hasRenderableMesh;
			prop.useJoltCollider = wantsCollision && hasRenderableMesh;

			TraceLog(
				LOG_INFO,
				"Imported GLB node: %s parent=%i mesh=%i local=(%.2f %.2f %.2f) world=(%.2f %.2f %.2f)",
				nodeName.c_str(),
				parentPropIndex,
				modelView != nullptr ? 1 : 0,
				localPosition.x, localPosition.y, localPosition.z,
				worldPosition.x, worldPosition.y, worldPosition.z
			);

			importedCount++;

			if (importedCount % 10 == 0)
			{
				LoadingPulse(
					TextFormat("Importing scene props... %i", importedCount),
					0.69f,
					false
				);
			}

			for (cgltf_size i = 0; i < node->children_count; i++)
			{
				visitNode(node->children[i], currentPropIndex);
			}

		};

	for (cgltf_size i = 0; i < scene->nodes_count; i++)
	{
		visitNode(scene->nodes[i], -1);
	}

	LoadingPulse("Updating imported scene transforms...", 0.71f, true);
	UpdateScenePropWorldTransforms();

	LoadingPulse("Applying imported collision rules...", 0.72f, true);
	ApplyImportedCollisionNamingRules();


	MarkShadowTextureBindingsDirty(false);

	TraceLog(
		LOG_INFO,
		"Imported GLB scene as props: %s, props=%i",
		glbPath,
		importedCount
	);

	TraceLog(
		LOG_INFO,
		"ImportGlbSceneAsProps total time: %.2f ms, imported props=%i",
		(GetTime() - importStart) * 1000.0,
		importedCount
	);

	cgltf_free(data);
}

void Game::RepairScenePropHierarchyLinks()
{
	for (SceneProp& prop : sceneProps)
	{
		prop.childIndices.clear();
	}

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		SceneProp& prop = sceneProps[i];

		if (prop.parentIndex < 0)
			continue;

		if (prop.parentIndex >= (int)sceneProps.size())
		{
			prop.parentIndex = -1;
			continue;
		}

		if (prop.parentIndex == i)
		{
			prop.parentIndex = -1;
			continue;
		}

		sceneProps[prop.parentIndex].childIndices.push_back(i);
	}
}
Matrix Game::GetImportedEditorDrawMatrix(const SceneProp& prop) const
{
	bool noOffset =
		fabsf(prop.importedEditorOffset.x) < 0.0001f &&
		fabsf(prop.importedEditorOffset.y) < 0.0001f &&
		fabsf(prop.importedEditorOffset.z) < 0.0001f;

	bool noRotation =
		fabsf(prop.importedEditorRotationDeg.x) < 0.0001f &&
		fabsf(prop.importedEditorRotationDeg.y) < 0.0001f &&
		fabsf(prop.importedEditorRotationDeg.z) < 0.0001f;

	bool noScale =
		fabsf(prop.importedEditorScale.x - 1.0f) < 0.0001f &&
		fabsf(prop.importedEditorScale.y - 1.0f) < 0.0001f &&
		fabsf(prop.importedEditorScale.z - 1.0f) < 0.0001f;

	if (noOffset && noRotation && noScale)
	{
		return MatrixIdentity();
	}

	Vector3 pivot = prop.importBasePosition;

	Quaternion q = QuaternionFromEuler(
		prop.importedEditorRotationDeg.x * DEG2RAD,
		prop.importedEditorRotationDeg.y * DEG2RAD,
		prop.importedEditorRotationDeg.z * DEG2RAD
	);

	Matrix toPivot = MatrixTranslate(
		-pivot.x,
		-pivot.y,
		-pivot.z
	);

	Matrix scale = MatrixScale(
		prop.importedEditorScale.x,
		prop.importedEditorScale.y,
		prop.importedEditorScale.z
	);

	Matrix rot = QuaternionToMatrix(q);

	Matrix back = MatrixTranslate(
		pivot.x + prop.importedEditorOffset.x,
		pivot.y + prop.importedEditorOffset.y,
		pivot.z + prop.importedEditorOffset.z
	);

	return MatrixMultiply(
		MatrixMultiply(
			MatrixMultiply(toPivot, scale),
			rot
		),
		back
	);
}
void Game::SyncImportedEditorOffsetFromRuntime(SceneProp& prop)
{
	if (!prop.importedFromGlbScene || prop.model == nullptr)
		return;

	prop.importedEditorOffset = Vector3Subtract(
		prop.position,
		prop.importBasePosition
	);

	prop.importedEditorRotationDeg = Vector3Subtract(
		prop.rotationDeg,
		prop.importBaseRotationDeg
	);

	// IMPORTANT:
	// Do not touch importedEditorScale here.
	// Imported GLB meshes are already baked at their scene scale.
	// Changing this during pickup can double-scale the visual.
}


void Game::ApplyImportedEditorTransformToRuntime(SceneProp& prop)
{
	if (!prop.importedFromGlbScene)
		return;

	prop.importBaseScale.x = SafeNonZeroScale(prop.importBaseScale.x);
	prop.importBaseScale.y = SafeNonZeroScale(prop.importBaseScale.y);
	prop.importBaseScale.z = SafeNonZeroScale(prop.importBaseScale.z);

	prop.importedEditorScale.x = SafeNonZeroScale(prop.importedEditorScale.x);
	prop.importedEditorScale.y = SafeNonZeroScale(prop.importedEditorScale.y);
	prop.importedEditorScale.z = SafeNonZeroScale(prop.importedEditorScale.z);

	prop.position = Vector3Add(
		prop.importBasePosition,
		prop.importedEditorOffset
	);

	prop.rotationDeg = Vector3Add(
		prop.importBaseRotationDeg,
		prop.importedEditorRotationDeg
	);

	prop.scale = {
		prop.importBaseScale.x * prop.importedEditorScale.x,
		prop.importBaseScale.y * prop.importedEditorScale.y,
		prop.importBaseScale.z * prop.importedEditorScale.z
	};

	prop.scale.x = SafeNonZeroScale(prop.scale.x);
	prop.scale.y = SafeNonZeroScale(prop.scale.y);
	prop.scale.z = SafeNonZeroScale(prop.scale.z);
}


void Game::DrawCustomerTreeNode(int customerIndex)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	Customer& customer = customers[customerIndex];

	ImGuiTreeNodeFlags flags =
		ImGuiTreeNodeFlags_Leaf |
		ImGuiTreeNodeFlags_SpanAvailWidth;

	if (selectedCustomerIndex == customerIndex)
		flags |= ImGuiTreeNodeFlags_Selected;

	const char* stateName = "Idle";

	switch (customer.animState)
	{
	case CustomerAnimState::Idle:     stateName = "Idle"; break;
	case CustomerAnimState::Walking:  stateName = "Walking"; break;
	case CustomerAnimState::Emote:    stateName = "Emote"; break;
	case CustomerAnimState::Point:    stateName = "Point"; break;
	case CustomerAnimState::LeftHand: stateName = "LeftHand"; break;
	case CustomerAnimState::LeftLook: stateName = "LeftLook"; break;
	case CustomerAnimState::Think:    stateName = "Think"; break;
	case CustomerAnimState::Give:     stateName = "Give"; break;
	case CustomerAnimState::Dance:    stateName = "Dance"; break;
	case CustomerAnimState::Twerk:    stateName = "Twerk"; break;
	}

	std::string label =
		"Customer " + std::to_string(customerIndex) +
		" [" + stateName + "]";

	if (customer.editorFrozen)
		label += " [Frozen]";

	if (customer.hasMoveTarget)
		label += " [Moving]";

	bool opened = ImGui::TreeNodeEx(
		(void*)(intptr_t)(100000 + customerIndex),
		flags,
		"%s",
		label.c_str()
	);

	if (ImGui::IsItemClicked())
	{
		selectedCustomerIndex = customerIndex;
		selectedScenePropIndex = -1;
		selectedInstancedPropIndex = -1;
		selectedItemPlacementSpotIndex = -1;

		selectedEditorItemIndex = -1;
	}

	if (opened)
	{
		ImGui::TreePop();
	}
}
bool Game::ScenePropSubtreeContainsIndex(int propIndex, int targetIndex) const
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return false;

	if (propIndex == targetIndex)
		return true;

	const SceneProp& prop = sceneProps[propIndex];

	for (int childIndex : prop.childIndices)
	{
		if (ScenePropSubtreeContainsIndex(childIndex, targetIndex))
			return true;
	}

	return false;
}
void Game::DrawScenePropTreeNode(int propIndex)
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return;


	SceneProp& prop = sceneProps[propIndex];

	ImGuiTreeNodeFlags flags =
		ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_SpanAvailWidth;

	if (prop.childIndices.empty())
		flags |= ImGuiTreeNodeFlags_Leaf;

	if (selectedScenePropIndex == propIndex)
		flags |= ImGuiTreeNodeFlags_Selected;

	bool hasMesh = prop.model != nullptr;

	std::string label = prop.name;

	if (!hasMesh)
		label += " [Empty]";

	if (prop.hasCollision)
		label += " [COL]";

	bool containsSelected = ScenePropSubtreeContainsIndex(
		propIndex,
		selectedScenePropIndex
	);

	if (containsSelected)
	{
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	}

	bool opened = ImGui::TreeNodeEx(
		(void*)(intptr_t)propIndex,
		flags,
		"%s",
		label.c_str()
	);

	if (selectedScenePropIndex == propIndex && sceneHierarchyScrollToSelected)
	{
		ImGui::SetScrollHereY(0.5f);
		sceneHierarchyScrollToSelected = false;
	}


	if (ImGui::IsItemClicked())
	{
		selectedScenePropIndex = propIndex;
		selectedInstancedPropIndex = -1;
		selectedCustomerIndex = -1;
		selectedItemPlacementSpotIndex = -1;
		selectedEditorItemIndex = -1;
	}

	if (opened)
	{
		for (int childIndex : prop.childIndices)
		{
			DrawScenePropTreeNode(childIndex);
		}

		ImGui::TreePop();
	}
}

void Game::DrawSelectedCustomerInspectorPanel()
{
	if (selectedCustomerIndex < 0 ||
		selectedCustomerIndex >= (int)customers.size())
	{
		return;
	}

	Customer& customer = customers[selectedCustomerIndex];

	ImGui::Text("Selected Customer");
	ImGui::Text("Index: %i", selectedCustomerIndex);
	ImGui::Text("Dialogue Script: %s", customer.dialogueScriptId.c_str());
	ImGui::Text("POI Group: %s", customer.poiGroup.c_str());

	ImGui::Separator();

	bool changed = false;

	if (ImGui::DragFloat3("Position", &customer.position.x, 0.05f))
	{
		changed = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat("Yaw", &customer.yawDeg, 1.0f, -360.0f, 360.0f))
	{
		changed = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat("Move Speed", &customer.moveSpeed, 0.05f, 0.0f, 5.0f))
	{
		changed = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat3("Scale", &customer.scale.x, 0.001f, 0.001f, 1.0f))
	{
		changed = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::Checkbox("Editor Frozen", &customer.editorFrozen))
	{
		changed = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::Checkbox("Use POI Navigation", &customer.usePOINavigation))
	{
		changed = true;
	}
	PushUndoIfItemActivated();

	if (changed)
	{
		ApplyCustomerEditorTransform(selectedCustomerIndex);
	}

	ImGui::Text("Current POI: %i", customer.currentPOIIndex);
	ImGui::Text("Target POI: %i", customer.targetPOIIndex);
	ImGui::Text("Destination POI: %i", customer.destinationPOIIndex);
	ImGui::Text("Path Waypoints: %i", (int)customer.pathWaypoints.size());
	ImGui::Text("Has Move Target: %s", customer.hasMoveTarget ? "Yes" : "No");

	if (changed)
	{
		ApplyCustomerEditorTransform(selectedCustomerIndex);
	}

	if (ImGui::Button("Stop Customer"))
	{
		PushUndoSnapshot();

		customer.hasMoveTarget = false;
		customer.pathWaypoints.clear();
		customer.pathWaypointCursor = 0;
		customer.targetPOIIndex = -1;
		customer.destinationPOIIndex = -1;
		customer.poiWaitTimer = 0.0f;
		customer.movementPauseTimer = 0.0f;
		customer.repathTimer = 0.0f;
		customer.SetAnimState(CustomerAnimState::Idle);

		ApplyCustomerEditorTransform(selectedCustomerIndex);
	}

	ImGui::SameLine();

	if (ImGui::Button("Unstuck Here"))
	{
		customer.editorFrozen = false;
		customer.hasMoveTarget = false;
		customer.pathWaypoints.clear();
		customer.pathWaypointCursor = 0;
		customer.targetPosition = customer.position;
		customer.poiWaitTimer = 0.5f;
		customer.movementPauseTimer = 0.0f;
		customer.repathTimer = 0.0f;
		customer.SetAnimState(CustomerAnimState::Idle);

		ApplyCustomerEditorTransform(selectedCustomerIndex);
	}

	if (ImGui::Button("Send To Selected POI") &&
		selectedCustomerPOI >= 0 &&
		selectedCustomerPOI < (int)customerPOIs.size())
	{
		customer.editorFrozen = false;
		StartCustomerRouteToPOI(selectedCustomerIndex, selectedCustomerPOI);
	}
	ImGui::Text("AI State: %i", (int)customer.aiState);
	ImGui::Text("Role: %s", customer.role == CustomerRole::Seller ? "Seller" : "Browser");
	ImGui::Text("Assigned Queue Slot: %i", customer.assignedQueueSlotIndex);

	if (customer.assignedQueueSlotIndex >= 0 &&
		customer.assignedQueueSlotIndex < (int)customerPOIs.size())
	{
		ImGui::Text(
			"Assigned Queue Order: %i",
			GetPOIQueueOrder(customer.assignedQueueSlotIndex)
		);
	}
}

void Game::ApplyCustomerEditorTransform(int customerIndex)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	Customer& customer = customers[customerIndex];

	customer.targetPosition = customer.position;

	customer.hasMoveTarget = false;
	customer.pathWaypoints.clear();
	customer.pathWaypointCursor = 0;

	customer.targetPOIIndex = -1;
	customer.destinationPOIIndex = -1;

	customer.poiWaitTimer = 0.0f;
	customer.movementPauseTimer = 0.0f;
	customer.repathTimer = 0.0f;

	customer.SetAnimState(CustomerAnimState::Idle);

	if (physics &&
		customerIndex >= 0 &&
		customerIndex < (int)customerBodyIds.size())
	{
		JPH::BodyID bodyId = customerBodyIds[customerIndex];

		if (!bodyId.IsInvalid())
		{
			Vector3 center = GetCustomerColliderCenter(customer);
			Quaternion rotation = GetCustomerColliderRotation(customer);

			physics->SetBodyPosition(bodyId, center);
			physics->SetBodyRotation(bodyId, rotation);
			physics->SetBodyLinearVelocity(bodyId, { 0.0f, 0.0f, 0.0f });
			physics->SetBodyAngularVelocity(bodyId, { 0.0f, 0.0f, 0.0f });
		}
	}
}

void Game::DrawInstancedPropHierarchySection()
{
	if (!ImGui::TreeNodeEx("Instanced Props", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	if (ImGui::Button("Add Basket Instance"))
	{
		PushUndoSnapshot();

		InstancedProp prop{};

		prop.name = TextFormat("Basket_%02i", (int)instancedProps.size());
		prop.type = InstancePropType::Basket;

		prop.position = { 0.0f, 0.0f, 0.0f };
		prop.rotationDeg = { 0.0f, 0.0f, 0.0f };
		prop.scale = { 1.0f, 1.0f, 1.0f };

		prop.visible = true;
		prop.hasCollision = true;
		prop.blocksPlayer = true;
		prop.autoCollider = true;

		if (basketInstanceColliderReady)
		{
			prop.colliderSize = basketInstanceColliderSize;
			prop.colliderOffset = basketInstanceColliderOffset;
		}

		prop.bodyId = JPH::BodyID();

		instancedProps.push_back(prop);
		selectedInstancedPropIndex = (int)instancedProps.size() - 1;

		instancedPropsDirty = true;
		RebuildInstancedPropTransforms();

		if (physics)
		{
			RebuildInstancedPropCollider(selectedInstancedPropIndex);
		}

		customerNavGridDirty = true;
		MarkShadowMapsDirty();
	}

	ImGui::Separator();

	for (int i = 0; i < (int)instancedProps.size(); i++)
	{
		InstancedProp& prop = instancedProps[i];

		ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_Leaf |
			ImGuiTreeNodeFlags_NoTreePushOnOpen;

		if (selectedInstancedPropIndex == i)
			flags |= ImGuiTreeNodeFlags_Selected;

		std::string label = prop.name + "##InstancedProp" + std::to_string(i);

		ImGui::TreeNodeEx(label.c_str(), flags);

		if (ImGui::IsItemClicked())
		{
			selectedInstancedPropIndex = i;

			selectedScenePropIndex = -1;
			selectedCustomerIndex = -1;
			selectedItemPlacementSpotIndex = -1;
			selectedEditorItemIndex = -1;
		}
	}

	ImGui::TreePop();
}

void Game::DrawScenePropHierarchyPanel()
{
	ImGui::Begin("Scene Hierarchy");

	ImGui::Text("Customers: %i", (int)customers.size());
	ImGui::Text("Scene Props: %i", (int)sceneProps.size());
	ImGui::Text("Instanced Props: %i", (int)instancedProps.size());
	ImGui::Text("Placement Spots: %i", (int)itemPlacementSpots.size());

	if (ImGui::Button("Rebuild Prop World Transforms"))
	{
		UpdateScenePropWorldTransforms();
		RecreatePhysicsWorld();
		MarkShadowMapsDirty();
	}

	ImGui::Separator();

	if (ImGui::BeginChild("ScenePropTree", ImVec2(0, 0), true))
	{
		if (ImGui::TreeNodeEx("Customers", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (int i = 0; i < (int)customers.size(); i++)
			{
				DrawCustomerTreeNode(i);
			}

			ImGui::TreePop();
		}

		ImGui::Separator();

		if (ImGui::TreeNodeEx("Scene Props", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (int i = 0; i < (int)sceneProps.size(); i++)
			{
				if (sceneProps[i].parentIndex == -1)
				{
					DrawScenePropTreeNode(i);
				}
			}

			ImGui::TreePop();
		}

		ImGui::Separator();

		DrawItemPlacementSpotHierarchySection();

		ImGui::Separator();

		DrawInstancedPropHierarchySection();
	}

	ImGui::EndChild();
	ImGui::End();
}
void Game::DrawSelectedInstancedPropInspectorPanel()
{
	if (selectedInstancedPropIndex < 0 ||
		selectedInstancedPropIndex >= (int)instancedProps.size())
	{
		return;
	}

	InstancedProp& prop = instancedProps[selectedInstancedPropIndex];

	ImGui::Text("Selected Instanced Prop");
	ImGui::Separator();

	const char* typeName = "Unknown";

	switch (prop.type)
	{
	case InstancePropType::GachaMachine:
		typeName = "GachaMachine";
		break;

	case InstancePropType::Basket:
		typeName = "Basket";
		break;
	}

	ImGui::Text("Type: %s", typeName);

	bool changed = false;

	char nameBuffer[128] = {};
	strncpy_s(nameBuffer, prop.name.c_str(), sizeof(nameBuffer) - 1);

	if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
	{
		prop.name = nameBuffer;
		changed = true;
	}
	PushUndoIfItemActivated();

	bool colliderNeedsRebuild = false;

	if (ImGui::Checkbox("Visible", &prop.visible))
	{
		changed = true;
		colliderNeedsRebuild = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::Checkbox("Has Collision", &prop.hasCollision))
	{
		changed = true;
		colliderNeedsRebuild = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::Checkbox("Auto Collider", &prop.autoCollider))
	{
		changed = true;
		colliderNeedsRebuild = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat3("Position", &prop.position.x, 0.05f))
	{
		changed = true;
	}
	if (ImGui::IsItemDeactivatedAfterEdit())
	{
		colliderNeedsRebuild = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat3("Rotation", &prop.rotationDeg.x, 1.0f))
	{
		changed = true;
	}
	if (ImGui::IsItemDeactivatedAfterEdit())
	{
		colliderNeedsRebuild = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat3("Scale", &prop.scale.x, 0.01f, 0.01f, 100.0f))
	{
		changed = true;
	}
	if (ImGui::IsItemDeactivatedAfterEdit())
	{
		colliderNeedsRebuild = true;
	}
	PushUndoIfItemActivated();


	if (!prop.autoCollider)
	{
		if (ImGui::DragFloat3("Collider Offset", &prop.colliderOffset.x, 0.01f))
		{
			changed = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			colliderNeedsRebuild = true;
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat3("Collider Size", &prop.colliderSize.x, 0.01f, 0.01f, 100.0f))
		{
			changed = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			colliderNeedsRebuild = true;
		}
		PushUndoIfItemActivated();
	}
	else
	{
		ImGui::Text(
			"Auto Collider Size: %.2f %.2f %.2f",
			prop.colliderSize.x,
			prop.colliderSize.y,
			prop.colliderSize.z
		);
	}

	if (ImGui::Button("Duplicate Instance"))
	{
		PushUndoSnapshot();

		InstancedProp copy = prop;
		copy.name += "_Copy";
		copy.position.x += 0.85f;
		copy.bodyId = JPH::BodyID();

		instancedProps.push_back(copy);
		selectedInstancedPropIndex = (int)instancedProps.size() - 1;

		instancedPropsDirty = true;
		RebuildInstancedPropTransforms();
		RebuildInstancedPropCollider(selectedInstancedPropIndex);

		customerNavGridDirty = true;
		StopAllCustomerRoaming();

		MarkShadowMapsDirty();
	}

	ImGui::SameLine();

	if (ImGui::Button("Delete Instance"))
	{
		PushUndoSnapshot();

		RemoveInstancedPropCollider(selectedInstancedPropIndex);

		instancedProps.erase(instancedProps.begin() + selectedInstancedPropIndex);
		selectedInstancedPropIndex = -1;

		instancedPropsDirty = true;
		RebuildInstancedPropTransforms();

		customerNavGridDirty = true;
		StopAllCustomerRoaming();

		MarkShadowMapsDirty();
		return;
	}

	if (changed)
	{
		instancedPropsDirty = true;
		RebuildInstancedPropTransforms();

		customerNavGridDirty = true;

		MarkShadowMapsDirty();
	}

	if (colliderNeedsRebuild)
	{
		RebuildInstancedPropCollider(selectedInstancedPropIndex);

		customerNavGridDirty = true;
		StopAllCustomerRoaming();

		MarkShadowMapsDirty();
	}


}

void Game::DrawSelectedItemPlacementSpotInspectorPanel()
{
	if (selectedItemPlacementSpotIndex < 0 ||
		selectedItemPlacementSpotIndex >= (int)itemPlacementSpots.size())
	{
		ImGui::Text("No placement spot selected.");
		return;
	}

	ItemPlacementSpot& spot = itemPlacementSpots[selectedItemPlacementSpotIndex];

	ImGui::Text("Selected Item Placement Spot");
	ImGui::Text("Index: %i", selectedItemPlacementSpotIndex);
	ImGui::Text("Occupied Prop: %i", spot.occupiedScenePropIndex);
	ImGui::Separator();

	bool changed = false;

	char idBuffer[128] = {};
	strncpy_s(idBuffer, spot.id.c_str(), sizeof(idBuffer) - 1);

	if (ImGui::InputText("ID", idBuffer, sizeof(idBuffer)))
	{
		spot.id = idBuffer;
		changed = true;
	}
	PushUndoIfItemActivated();

	int kindValue = (int)spot.kind;

	const char* kindNames[] =
	{
		"Counter Offer",
		"Counter Scan",
		"Shelf Slot"
	};

	if (ImGui::Combo("Kind", &kindValue, kindNames, IM_ARRAYSIZE(kindNames)))
	{
		spot.kind = (ItemPlacementSpotKind)kindValue;
		changed = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat3("Position", &spot.position.x, 0.05f))
	{
		changed = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat3("Rotation", &spot.rotationDeg.x, 1.0f))
	{
		changed = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat("Snap Radius", &spot.snapRadius, 0.05f, 0.05f, 5.0f))
	{
		changed = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::Checkbox("Enabled", &spot.enabled))
	{
		changed = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::Checkbox("Allow Player Drop", &spot.allowPlayerDrop))
	{
		changed = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::Checkbox("Allow Customer Place", &spot.allowCustomerPlace))
	{
		changed = true;
	}
	PushUndoIfItemActivated();

	char tagBuffer[128] = {};
	strncpy_s(tagBuffer, spot.acceptedItemTag.c_str(), sizeof(tagBuffer) - 1);

	if (ImGui::InputText("Accepted Item Tag", tagBuffer, sizeof(tagBuffer)))
	{
		spot.acceptedItemTag = tagBuffer;
		changed = true;
	}
	PushUndoIfItemActivated();

	if (spot.kind == ItemPlacementSpotKind::CounterOffer ||
		spot.kind == ItemPlacementSpotKind::CounterScan)
	{
		ImGui::TextWrapped("Counter spots ignore Accepted Item Tag and accept all objects.");
	}

	if (ImGui::Button("Move Spot To Player"))
	{
		PushUndoSnapshot();

		spot.position = player.m_pos;
		spot.position.y += 1.0f;
		spot.rotationDeg = { 0.0f, player.yaw * RAD2DEG, 0.0f };

		changed = true;
	}

	ImGui::SameLine();

	if (ImGui::Button("Delete Spot"))
	{
		PushUndoSnapshot();

		for (SceneProp& prop : sceneProps)
		{
			if (prop.currentPlacementSpotIndex == selectedItemPlacementSpotIndex)
				prop.currentPlacementSpotIndex = -1;

			if (prop.homePlacementSpotIndex == selectedItemPlacementSpotIndex)
				prop.homePlacementSpotIndex = -1;
		}

		itemPlacementSpots.erase(
			itemPlacementSpots.begin() + selectedItemPlacementSpotIndex
		);

		selectedItemPlacementSpotIndex = -1;
		RebuildItemPlacementSpotOccupancy();

		return;
	}

	if (changed)
	{
		RebuildItemPlacementSpotOccupancy();
		MarkShadowMapsDirty();
	}
}
void Game::DrawSelectedScenePropInspectorPanel()
{
	ImGui::Begin("Selected Inspector");

	if (selectedCustomerIndex >= 0 &&
		selectedCustomerIndex < (int)customers.size())
	{
		DrawSelectedCustomerInspectorPanel();
		ImGui::End();
		return;
	}

	if (selectedInstancedPropIndex >= 0 &&
		selectedInstancedPropIndex < (int)instancedProps.size())
	{
		DrawSelectedInstancedPropInspectorPanel();
		ImGui::End();
		return;
	}

	if (selectedItemPlacementSpotIndex >= 0 &&
		selectedItemPlacementSpotIndex < (int)itemPlacementSpots.size())
	{
		DrawSelectedItemPlacementSpotInspectorPanel();
		ImGui::End();
		return;
	}

	if (selectedScenePropIndex < 0 || selectedScenePropIndex >= (int)sceneProps.size())
	{
		ImGui::Text("No scene prop selected.");
		ImGui::End();
		return;
	}

	if (selectedScenePropIndex < 0 || selectedScenePropIndex >= (int)sceneProps.size())
	{
		ImGui::Text("No scene prop selected.");
		ImGui::End();
		return;
	}

	SceneProp& prop = sceneProps[selectedScenePropIndex];

	ImGui::Text("Name: %s", prop.name.c_str());
	ImGui::Text("Index: %i", selectedScenePropIndex);
	ImGui::Text("Parent: %i", prop.parentIndex);
	ImGui::Text("Children: %i", (int)prop.childIndices.size());
	ImGui::Text("Has Mesh: %s", prop.model != nullptr ? "Yes" : "No");
	ImGui::Separator();

	bool changed = false;
	bool transformChanged = false;
	bool physicsNeedsRebuild = false;

	bool isImportedRenderable =
		prop.importedFromGlbScene &&
		prop.model != nullptr;

	if (isImportedRenderable)
	{
		ImGui::TextColored(
			ImVec4(0.5f, 0.8f, 1.0f, 1.0f),
			"Imported GLB Mesh Transform"
		);

		ImGui::TextWrapped(
			"Use these offset controls for imported mesh props. "
			"Do not use Local Position for imported renderable GLB meshes."
		);

		if (ImGui::DragFloat3("Visual Offset", &prop.importedEditorOffset.x, 0.05f))
		{
			changed = true;
			transformChanged = true;
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat3("Visual Rotation Offset", &prop.importedEditorRotationDeg.x, 0.5f))
		{
			changed = true;
			transformChanged = true;
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat3("Visual Scale Multiplier", &prop.importedEditorScale.x, 0.01f, 0.01f, 100.0f))
		{
			prop.importedEditorScale.x = fmaxf(prop.importedEditorScale.x, 0.01f);
			prop.importedEditorScale.y = fmaxf(prop.importedEditorScale.y, 0.01f);
			prop.importedEditorScale.z = fmaxf(prop.importedEditorScale.z, 0.01f);

			changed = true;
			transformChanged = true;
			physicsNeedsRebuild = true;
		}
		PushUndoIfItemActivated();

		if (ImGui::Button("Reset Imported Visual Offset"))
		{
			PushUndoSnapshot();

			prop.importedEditorOffset = { 0.0f, 0.0f, 0.0f };
			prop.importedEditorRotationDeg = { 0.0f, 0.0f, 0.0f };
			prop.importedEditorScale = { 1.0f, 1.0f, 1.0f };

			changed = true;
			transformChanged = true;
			physicsNeedsRebuild = true;
		}
	}
	else
	{
		if (ImGui::DragFloat3("Local Position", &prop.localPosition.x, 0.05f))
		{
			changed = true;
			transformChanged = true;
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat3("Local Rotation", &prop.localRotationDeg.x, 0.5f))
		{
			changed = true;
			transformChanged = true;
		}
		PushUndoIfItemActivated();

		if (ImGui::DragFloat3("Local Scale", &prop.localScale.x, 0.01f, 0.01f, 100.0f))
		{
			changed = true;
			transformChanged = true;
			physicsNeedsRebuild = true;
		}
		PushUndoIfItemActivated();
	}

	ImGui::Separator();

	ImGui::Text("World Position: %.2f, %.2f, %.2f", prop.position.x, prop.position.y, prop.position.z);
	ImGui::Text("World Rotation: %.2f, %.2f, %.2f", prop.rotationDeg.x, prop.rotationDeg.y, prop.rotationDeg.z);
	ImGui::Text("World Scale: %.2f, %.2f, %.2f", prop.scale.x, prop.scale.y, prop.scale.z);

	ImGui::Separator();

	if (ImGui::Checkbox("Visible", &prop.visible))
	{
		changed = true;
		MarkShadowMapsDirty();
	}
	PushUndoIfItemActivated();

	if (ImGui::Checkbox("Has Collision", &prop.hasCollision))
	{
		changed = true;
		physicsNeedsRebuild = true;
	}
	PushUndoIfItemActivated();
	if (ImGui::Checkbox("Casts Shadow", &prop.castsShadow))
	{
		changed = true;
		MarkShadowMapsDirty();
	}
	PushUndoIfItemActivated();

	if (ImGui::Checkbox("Can Occlude", &prop.canOcclude))
	{
		changed = true;
	}
	if (prop.canOcclude)
	{
		Rectangle rect{};
		float depth = 0.0f;

		Camera3D activeCam = editMode ? editorCamera : camera;

		bool hasRect = GetScenePropScreenRect(
			selectedScenePropIndex,
			activeCam,
			rect,
			depth
		);

		ImGui::Separator();
		ImGui::Text("Occlusion Debug");
		ImGui::Text("Needs Transparent Pass: %s", ScenePropNeedsTransparentPass(prop) ? "Yes" : "No");
		ImGui::Text("Has Screen Rect: %s", hasRect ? "Yes" : "No");

		if (hasRect)
		{
			ImGui::Text(
				"Rect: %.1f %.1f %.1f %.1f",
				rect.x,
				rect.y,
				rect.width,
				rect.height
			);

			ImGui::Text("Area: %.1f", rect.width * rect.height);
			ImGui::Text("Depth: %.2f", depth);
			ImGui::Text("Min Area: %.1f", minOccluderScreenArea);
		}

		if (ImGui::Button("Force Selected Prop Opaque"))
		{
			prop.transparentMaterialIndices.clear();
			prop.transparentAlpha = 255;

			for (int& mode : prop.materialAlphaModes)
				mode = 0;

			for (int& mode : prop.meshAlphaModes)
				mode = 0;

			for (float& alpha : prop.meshBaseAlphas)
				alpha = 1.0f;

			MarkShadowMapsDirty();
		}

	}
	PushUndoIfItemActivated();

	if (ImGui::Checkbox("Ignore Occlusion Culling", &prop.ignoreOcclusionCulling))
	{
		changed = true;
	}
	PushUndoIfItemActivated();

	if (prop.canOcclude)
	{
		ImGui::TextWrapped(
			"This prop can hide other props during screen-space occlusion culling."
		);
	}

	if (prop.ignoreOcclusionCulling)
	{
		ImGui::TextWrapped(
			"This prop will always draw if it passes normal frustum culling."
		);
	}

	if (ImGui::Checkbox("Blocks Player", &prop.blocksPlayer))
	{
		changed = true;
		physicsNeedsRebuild = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::Checkbox("Use Jolt Collider", &prop.useJoltCollider))
	{
		changed = true;
		physicsNeedsRebuild = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat3("Collider Offset", &prop.colliderOffset.x, 0.01f))
	{
		changed = true;
		transformChanged = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat3("Collider Size", &prop.colliderSize.x, 0.01f, 0.01f, 100.0f))
	{
		prop.colliderSize.x = fmaxf(prop.colliderSize.x, 0.01f);
		prop.colliderSize.y = fmaxf(prop.colliderSize.y, 0.01f);
		prop.colliderSize.z = fmaxf(prop.colliderSize.z, 0.01f);

		changed = true;
		physicsNeedsRebuild = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::Button("Reset Collider To Model Bounds"))
	{
		ResetScenePropColliderToModelBounds(prop);
	}

	ImGui::SameLine();

	if (ImGui::Button("Rebuild Physics"))
	{
		CaptureLiveScenePropPhysicsTransforms();
		UpdateScenePropWorldTransforms();

		if (selectedScenePropIndex >= 0 && selectedScenePropIndex < (int)sceneProps.size())
		{
			SceneProp& selected = sceneProps[selectedScenePropIndex];

			bool isAggregateProp =
				selected.importedFromGlbScene &&
				(
					ShouldImportedNodeBeAggregateProp(selected.name) ||
					ShouldImportedNodeBeAggregateProp(selected.sourceNodeName)
					);

			if (isAggregateProp)
			{
				RebuildScenePropSubtreeCollider(selectedScenePropIndex);
				selected.hasCollision = true;
				selected.blocksPlayer = true;
				selected.useJoltCollider = true;
				selected.useNormalCollision = false;
			}
		}

		ApplyImportedCollisionNamingRules();
		RecreatePhysicsWorld();
		MarkShadowMapsDirty();
	}

	ImGui::Separator();

	if (ImGui::CollapsingHeader("Pickup / Hold Settings", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::Checkbox("Can Pickup", &prop.canPickup))
		{
			if (prop.canPickup)
			{
				prop.hasCollision = true;
				prop.blocksPlayer = true;
				prop.useJoltCollider = true;
				prop.useNormalCollision = false;
				prop.simulatePhysics = true;
				prop.syncFromPhysics = false;
				prop.editLockPhysics = true;
			}

			changed = true;
			physicsNeedsRebuild = true;
		}
		PushUndoIfItemActivated();

		if (prop.canPickup || prop.simulatePhysics)
		{
			if (ImGui::DragFloat3("Hold Offset", &prop.holdOffsetLocal.x, 0.01f, -2.0f, 2.0f))
			{
				changed = true;
			}
			PushUndoIfItemActivated();

			if (ImGui::DragFloat3("Hold Rotation Offset", &prop.holdRotationOffsetDeg.x, 1.0f, -360.0f, 360.0f))
			{
				changed = true;
			}
			PushUndoIfItemActivated();

			if (ImGui::Checkbox("Follow Camera Pitch", &prop.holdFollowCameraPitch))
			{
				changed = true;
			}
			PushUndoIfItemActivated();

			if (ImGui::Checkbox("Snap Upright On Drop", &prop.snapUprightOnDrop))
			{
				changed = true;
			}
			PushUndoIfItemActivated();

			if (ImGui::DragFloat3("Drop Rotation Offset", &prop.dropRotationOffsetDeg.x, 1.0f, -360.0f, 360.0f))
			{
				changed = true;
			}
			PushUndoIfItemActivated();

			if (ImGui::Button("Preset: Handheld Front"))
			{
				PushUndoSnapshot();

				prop.canPickup = true;
				prop.holdOffsetLocal = { 0.0f, -0.15f, -0.15f };
				prop.holdRotationOffsetDeg = { 0.0f, 90.0f, 0.0f };
				prop.holdFollowCameraPitch = true;
				prop.snapUprightOnDrop = true;
				prop.dropRotationOffsetDeg = { 0.0f, -90.0f, 0.0f };

				prop.hasCollision = true;
				prop.blocksPlayer = true;
				prop.useJoltCollider = true;
				prop.useNormalCollision = false;
				prop.simulatePhysics = true;
				prop.syncFromPhysics = false;
				prop.editLockPhysics = true;

				changed = true;
				physicsNeedsRebuild = true;
			}

			ImGui::SameLine();

			if (ImGui::Button("Flip Y 180"))
			{
				PushUndoSnapshot();

				prop.holdRotationOffsetDeg.y += 180.0f;

				changed = true;
			}
		}
	}
	if (ImGui::CollapsingHeader("Inspect Self Dialogue", ImGuiTreeNodeFlags_DefaultOpen))
	{
		char tagBuffer[128] = {};
		strncpy_s(tagBuffer, prop.inspectDialogueTag.c_str(), sizeof(tagBuffer) - 1);

		if (ImGui::InputText("Inspect Dialogue Tag", tagBuffer, sizeof(tagBuffer)))
		{
			prop.inspectDialogueTag = tagBuffer;
			changed = true;
		}
		PushUndoIfItemActivated();

		std::string joined = JoinLines(prop.inspectDialogueLines);

		char textBuffer[2048] = {};
		strncpy_s(textBuffer, joined.c_str(), sizeof(textBuffer) - 1);

		ImGui::TextWrapped("One line = one self-dialogue node during inspect.");

		if (ImGui::InputTextMultiline(
			"Inspect Lines",
			textBuffer,
			sizeof(textBuffer),
			ImVec2(-1.0f, 120.0f)
		))
		{
			prop.inspectDialogueLines = SplitLines(textBuffer);
			changed = true;
		}
		PushUndoIfItemActivated();

		if (ImGui::Button("Preset: Game Console Appraisal"))
		{
			PushUndoSnapshot();

			prop.inspectDialogueTag = "console";
			prop.inspectDialogueLines.clear();
			prop.inspectDialogueLines.push_back("Hmm... the shell looks clean, and the screen is not badly scratched.");
			prop.inspectDialogueLines.push_back("The buttons still feel responsive.");
			prop.inspectDialogueLines.push_back("If it powers on, this could probably sell for around 3000 yen.");

			changed = true;
		}
	}

	int categoryValue = (int)prop.pickupCategory;

	const char* categoryNames[] =
	{
		"Generic",
		"For Sale",
		"Selling",
		"Both"
	};

	if (ImGui::Combo(
		"Pickup Category",
		&categoryValue,
		categoryNames,
		IM_ARRAYSIZE(categoryNames)
	))
	{
		prop.pickupCategory = (PickupItemCategory)categoryValue;
		changed = true;
	}
	PushUndoIfItemActivated();

	char tradeBuffer[128] = {};
	strncpy_s(tradeBuffer, prop.tradeItemId.c_str(), sizeof(tradeBuffer) - 1);

	if (ImGui::InputText("Trade Item ID", tradeBuffer, sizeof(tradeBuffer)))
	{
		prop.tradeItemId = tradeBuffer;
		changed = true;
	}
	PushUndoIfItemActivated();

	char itemTagBuffer[128] = {};
	strncpy_s(
		itemTagBuffer,
		prop.itemTag.c_str(),
		sizeof(itemTagBuffer) - 1
	);

	if (ImGui::InputText("Item Tag", itemTagBuffer, sizeof(itemTagBuffer)))
	{
		prop.itemTag = itemTagBuffer;
		changed = true;
	}
	PushUndoIfItemActivated();

	if (ImGui::Button("Tag: PS4 Game"))
	{
		prop.itemTag = "ps4_game";
		changed = true;
	}
	ImGui::SameLine();

	if (ImGui::Button("Tag: Retro Console"))
	{
		prop.itemTag = "retro_console";
		changed = true;
	}

	if (ImGui::Button("Tag: Current Console"))
	{
		prop.itemTag = "current_console";
		changed = true;
	}
	ImGui::SameLine();

	if (ImGui::Button("Tag: Retro Cartridge"))
	{
		prop.itemTag = "retro_cartridge";
		changed = true;
	}

	if (changed)
	{
		if (isImportedRenderable)
		{
			ApplyImportedEditorTransformToRuntime(prop);
		}
		else
		{
			UpdateScenePropWorldTransforms();
		}

		if (physicsNeedsRebuild)
		{
			RecreatePhysicsWorld();
		}
		else if (transformChanged)
		{
			ApplyAllScenePropTransformsToBodies();
		}

		customerNavGridDirty = true;
		MarkShadowMapsDirty();
	}

	ImGui::End();
}

void Game::DrawScenePropShadow(const SceneProp& prop) const
{
	if (prop.model == nullptr)
		return;


	Matrix transform = GetScenePropDrawMatrix(prop);

	Shader oldShaders[64];
	int materialCount = prop.model->materialCount;
	int savedCount = materialCount;

	if (savedCount > 64)
		savedCount = 64;

	for (int i = 0; i < savedCount; i++)
	{
		oldShaders[i] = prop.model->materials[i].shader;
		prop.model->materials[i].shader = shadowDepthShader;
	}

	for (int i = 0; i < prop.model->meshCount; i++)
	{
		int matIndex = 0;

		if (prop.model->meshMaterial != nullptr)
			matIndex = prop.model->meshMaterial[i];

		if (matIndex < 0 || matIndex >= prop.model->materialCount)
			matIndex = 0;

		DrawMesh(
			prop.model->meshes[i],
			prop.model->materials[matIndex],
			transform
		);
	}

	for (int i = 0; i < savedCount; i++)
	{
		prop.model->materials[i].shader = oldShaders[i];
	}
}

bool Game::GetScenePropCullSphereWorld(
	const SceneProp& prop,
	Vector3& outCenter,
	float& outRadius
) const
{
	if (!prop.visible || prop.model == nullptr)
		return false;

	if (!prop.renderBoundsReady)
		return false;

	Matrix drawMatrix = GetScenePropDrawMatrix(prop);

	outCenter = Vector3Transform(prop.localRenderCenter, drawMatrix);

	float maxScale = 1.0f;

	if (!prop.importedFromGlbScene)
	{
		maxScale = fmaxf(
			fabsf(prop.scale.x),
			fmaxf(fabsf(prop.scale.y), fabsf(prop.scale.z))
		);
	}

	outRadius = prop.localRenderRadius * maxScale;

	if (!IsFiniteVector3(outCenter) || !IsFiniteFloat(outRadius))
		return false;

	return true;
}

void Game::DrawScenePropColliderDebug() const
{
	for (const SceneProp& prop : sceneProps)
	{
		if (!prop.hasCollision)
			continue;

		if (!prop.blocksPlayer)
			continue;

		Vector3 center = Vector3Add(prop.position, GetScenePropRotatedOffset(prop));

		Vector3 size = {
			fabsf(prop.colliderSize.x * prop.scale.x),
			fabsf(prop.colliderSize.y * prop.scale.y),
			fabsf(prop.colliderSize.z * prop.scale.z)
		};

		if (!IsFiniteVector3(center) || !IsFiniteVector3(size))
			continue;

		BoundingBox box;
		box.min = {
			center.x - size.x * 0.5f,
			center.y - size.y * 0.5f,
			center.z - size.z * 0.5f
		};

		box.max = {
			center.x + size.x * 0.5f,
			center.y + size.y * 0.5f,
			center.z + size.z * 0.5f
		};

		Color color = prop.bodyId.IsInvalid()
			? RED
			: GREEN;

		DrawBoundingBox(box, color);
	}
}
void Game::DrawScenePropOutlineWithMatrix(
	int propIndex,
	const Matrix& transform,
	const Camera3D& cam,
	bool redrawActualSceneProp
) const
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size()) return;
	if (outlineShader.id == 0) return;

	const SceneProp& prop = sceneProps[propIndex];
	if (!prop.visible || prop.model == nullptr) return;

	const OutlineModelCache* cache = GetOutlineModelCache(prop.model);
	if (cache == nullptr) return;

	float aspect = (float)GetScreenWidth() / (float)GetScreenHeight();

	Matrix view = MatrixLookAt(cam.position, cam.target, cam.up);
	Matrix projection = MatrixPerspective(
		cam.fovy * DEG2RAD,
		aspect,
		rlGetCullDistanceNear(),
		rlGetCullDistanceFar()
	);

	SetShaderValueMatrix(outlineShader, outlineViewLoc, view);
	SetShaderValueMatrix(outlineShader, outlineProjectionLoc, projection);

	Material outlineMat = prop.model->materials[0];
	outlineMat.shader = outlineShader;

	// PASS A: hidden fill through objects
	if (pickupHiddenFillEnabled && pickupHiddenFillColor.a > 0)
	{
		Vector4 hiddenFillColor = ColorNormalize(pickupHiddenFillColor);
		float hiddenFillWidth = pickupHiddenFillWidth;

		SetShaderValue(outlineShader, outlineColorLoc, &hiddenFillColor, SHADER_UNIFORM_VEC4);
		SetShaderValue(outlineShader, outlineWidthLoc, &hiddenFillWidth, SHADER_UNIFORM_FLOAT);

		BeginBlendMode(BLEND_ALPHA);

		rlDisableDepthTest();
		rlDisableDepthMask();

		rlEnableBackfaceCulling();
		rlSetCullFace(RL_CULL_FACE_BACK);

		for (const Mesh& mesh : cache->meshes)
		{
			DrawMesh(mesh, outlineMat, transform);
		}

		rlEnableDepthTest();
		rlEnableDepthMask();

		EndBlendMode();
	}

	// PASS B: occluded outline through objects
	if (pickupOccludedOutlineEnabled && pickupOccludedOutlineColor.a > 0)
	{
		Vector4 occludedOutlineColor = ColorNormalize(pickupOccludedOutlineColor);
		float occludedOutlineWidth = pickupOccludedOutlineWidth;

		SetShaderValue(outlineShader, outlineColorLoc, &occludedOutlineColor, SHADER_UNIFORM_VEC4);
		SetShaderValue(outlineShader, outlineWidthLoc, &occludedOutlineWidth, SHADER_UNIFORM_FLOAT);

		BeginBlendMode(BLEND_ALPHA);

		rlDisableDepthTest();
		rlDisableDepthMask();

		rlEnableBackfaceCulling();
		rlSetCullFace(RL_CULL_FACE_FRONT);

		for (const Mesh& mesh : cache->meshes)
		{
			DrawMesh(mesh, outlineMat, transform);
		}

		rlEnableDepthTest();
		rlEnableDepthMask();
		rlSetCullFace(RL_CULL_FACE_BACK);

		EndBlendMode();
	}

	// Only the real pickup highlight should redraw the actual object.
	// Placement preview should stay as ghost/outline only.
	if (redrawActualSceneProp)
	{
		DrawScenePropByIndex(propIndex);
	}

	// PASS C: visible outline
	{
		Color pulsedColor = GetCurrentVisibleOutlineColor();
		Vector4 outlineColor = ColorNormalize(pulsedColor);
		float outlineWidth = GetCurrentVisibleOutlineWidth();

		SetShaderValue(outlineShader, outlineColorLoc, &outlineColor, SHADER_UNIFORM_VEC4);
		SetShaderValue(outlineShader, outlineWidthLoc, &outlineWidth, SHADER_UNIFORM_FLOAT);

		BeginBlendMode(BLEND_ALPHA);

		rlEnableDepthTest();
		rlDisableDepthMask();

		rlEnableBackfaceCulling();
		rlSetCullFace(RL_CULL_FACE_FRONT);

		for (const Mesh& mesh : cache->meshes)
		{
			DrawMesh(mesh, outlineMat, transform);
		}

		rlSetCullFace(RL_CULL_FACE_BACK);
		rlEnableDepthMask();

		EndBlendMode();
	}
}

Matrix Game::GetScenePropPreviewDrawMatrix(
	const SceneProp& prop,
	Vector3 previewPosition,
	Vector3 previewRotationDeg
) const
{
	if (!prop.importedFromGlbScene)
	{
		return BuildModelMatrix(
			previewPosition,
			previewRotationDeg,
			prop.scale
		);
	}

	// Imported GLB scene props are baked into their original scene position.
	// So preview movement must use importedEditorOffset-style transform,
	// not a normal BuildModelMatrix.
	SceneProp temp = prop;

	temp.position = previewPosition;
	temp.rotationDeg = previewRotationDeg;

	temp.importedEditorOffset = Vector3Subtract(
		previewPosition,
		prop.importBasePosition
	);

	temp.importedEditorRotationDeg = Vector3Subtract(
		previewRotationDeg,
		prop.importBaseRotationDeg
	);

	// Keep importedEditorScale unchanged.
	// Do not derive preview scale from runtime physics scale.
	return GetImportedEditorDrawMatrix(temp);
}

Matrix Game::GetScenePropDrawMatrix(const SceneProp& prop) const
{
	if (prop.importedFromGlbScene)
	{
		return GetImportedEditorDrawMatrix(prop);
	}

	return BuildModelMatrix(
		prop.position,
		prop.rotationDeg,
		prop.scale
	);
}
bool Game::PickScenePropAtMouse(
	const Camera3D& cam,
	ScenePropPickResult& outResult
) const
{
	outResult = {};
	outResult.index = -1;
	outResult.hit = false;
	outResult.collision.hit = false;
	outResult.collision.distance = FLT_MAX;

	Ray ray = GetScreenToWorldRay(GetMousePosition(), cam);

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		const SceneProp& prop = sceneProps[i];

		if (!prop.visible)
			continue;

		if (prop.model == nullptr)
			continue;

		if (hasHeldBody && i == heldScenePropIndex)
			continue;

		BoundingBox worldBounds{};

		if (!GetScenePropRenderBoundsWorld(prop, worldBounds))
			continue;

		// Broad phase first.
		RayCollision boxHit = GetRayCollisionBox(ray, worldBounds);

		if (!boxHit.hit)
			continue;

		if (boxHit.distance >= outResult.collision.distance)
			continue;

		Model* model = prop.model;
		Matrix drawMatrix = GetScenePropDrawMatrix(prop);

		bool canDoMeshTest = false;
		bool meshHitFound = false;

		for (int m = 0; m < model->meshCount; m++)
		{
			const Mesh& mesh = model->meshes[m];

			if (mesh.vertices == nullptr)
				continue;

			canDoMeshTest = true;

			RayCollision meshHit = GetRayCollisionMesh(
				ray,
				mesh,
				drawMatrix
			);

			if (!meshHit.hit)
				continue;

			if (meshHit.distance < outResult.collision.distance)
			{
				outResult.index = i;
				outResult.collision = meshHit;
				outResult.hit = true;
				meshHitFound = true;
			}
		}

		// Fallback only if this mesh cannot be tested precisely.
		// This prevents large AABBs from selecting empty space around shelves.
		if (!canDoMeshTest && !meshHitFound)
		{
			if (boxHit.distance < outResult.collision.distance)
			{
				outResult.index = i;
				outResult.collision = boxHit;
				outResult.hit = true;
			}
		}
	}

	return outResult.hit;
}

void Game::UpdateEditorScenePicking(const Camera3D& cam)
{
	if (!editMode)
		return;

	ImGuiIO& io = ImGui::GetIO();

	// Do not pick through ImGui windows.
	if (io.WantCaptureMouse)
		return;

	// RMB is already used for editor camera look.
	if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
		return;

	ScenePropPickResult result{};

	if (!PickScenePropAtMouse(cam, result))
		return;

	selectedScenePropIndex = result.index;

	// Clear other editor selections so the inspector is unambiguous.
	selectedEditorItemIndex = -1;
	selectedInstancedPropIndex = -1;
	selectedCustomerIndex = -1;

	// Optional: reuse targeted index so your existing outline/debug can follow it.
	targetedScenePropIndex = result.index;

	sceneHierarchyScrollToSelected = true;

	TraceLog(
		LOG_INFO,
		"Selected scene prop by click: [%i] %s",
		result.index,
		sceneProps[result.index].name.c_str()
	);
}

bool Game::GetScenePropRenderBoundsWorld(
	const SceneProp& prop,
	BoundingBox& outBounds
) const
{
	if (!prop.visible || prop.model == nullptr)
		return false;

	BoundingBox localBounds = GetModelBoundingBox(*prop.model);
	Matrix drawMatrix = GetScenePropDrawMatrix(prop);

	Vector3 corners[8] =
	{
		{ localBounds.min.x, localBounds.min.y, localBounds.min.z },
		{ localBounds.max.x, localBounds.min.y, localBounds.min.z },
		{ localBounds.min.x, localBounds.max.y, localBounds.min.z },
		{ localBounds.max.x, localBounds.max.y, localBounds.min.z },

		{ localBounds.min.x, localBounds.min.y, localBounds.max.z },
		{ localBounds.max.x, localBounds.min.y, localBounds.max.z },
		{ localBounds.min.x, localBounds.max.y, localBounds.max.z },
		{ localBounds.max.x, localBounds.max.y, localBounds.max.z }
	};

	bool hasBounds = false;

	for (int i = 0; i < 8; i++)
	{
		Vector3 worldPoint = Vector3Transform(corners[i], drawMatrix);
		ExpandBoundsWithPoint(outBounds, worldPoint, hasBounds);
	}

	return hasBounds;
}
bool Game::IsLargeStaticShellPropName(const SceneProp& prop) const
{
	std::string n = prop.name + " " + prop.sourceNodeName;

	std::transform(
		n.begin(),
		n.end(),
		n.begin(),
		[](unsigned char c) { return (char)std::tolower(c); }
	);

	return
		n.find("wall") != std::string::npos ||
		n.find("ceiling") != std::string::npos ||
		n.find("roof") != std::string::npos ||
		n.find("floor") != std::string::npos ||
		n.find("ground") != std::string::npos ||
		n.find("outside") != std::string::npos ||
		n.find("pavement") != std::string::npos ||
		n.find("road") != std::string::npos;
}
bool Game::ShouldDrawSceneProp(const SceneProp& prop, const Camera3D& cam) const
{
	if (!prop.visible || prop.model == nullptr)
		return false;

	Vector3 center{};
	float radius = 1.0f;

	if (!GetScenePropCullSphereWorld(prop, center, radius))
		return false;

	const bool isShellProp = IsLargeStaticShellPropName(prop);

	if (isShellProp)
	{
		// Large shell props can have bad/awkward cull spheres because they are long,
		// thin, or have origins far from the visible surface.
		// So: skip frustum culling for these, but keep a generous distance cap.
		const float maxShellDistance = 120.0f;

		float distSqr = Vector3DistanceSqr(cam.position, center);

		if (distSqr > maxShellDistance * maxShellDistance)
			return false;

		return true;
	}

	const float maxDrawDistance = 45.0f;

	return IsSphereInCameraView(cam, center, radius, maxDrawDistance);
}
bool Game::ShouldScenePropCastShadow(const SceneProp& prop, const Light& light) const
{
	if (!prop.visible || prop.model == nullptr)
		return false;

	if (!prop.castsShadow)
		return false;

	if (!ShouldScenePropCastShadowByName(prop.name))
		return false;

	if (!ShouldScenePropCastShadowByName(prop.sourceNodeName))
		return false;

	// Only skip transparency if this prop is actually using transparent materials.
	bool hasBlendTransparency = false;

	for (int mode : prop.materialAlphaModes)
	{
		if (mode == 2)
		{
			hasBlendTransparency = true;
			break;
		}
	}

	for (int mode : prop.meshAlphaModes)
	{
		if (mode == 2)
		{
			hasBlendTransparency = true;
			break;
		}
	}

	if (!prop.transparentMaterialIndices.empty())
		hasBlendTransparency = true;

	if (hasBlendTransparency && prop.transparentAlpha < 250)
		return false;

	return true;
}
void Game::DrawScenePropRenderBoundsDebug() const
{
	for (const SceneProp& prop : sceneProps)
	{
		if (!prop.visible || prop.model == nullptr)
			continue;

		BoundingBox bounds{};

		if (!GetScenePropRenderBoundsWorld(prop, bounds))
			continue;

		DrawBoundingBox(bounds, BLUE);
	}
}
bool Game::ScenePropNeedsTransparentPass(const SceneProp& prop) const
{
	if (!prop.transparentMaterialIndices.empty())
		return true;

	for (int mode : prop.materialAlphaModes)
	{
		if (mode == 2)
			return true;
	}

	for (int mode : prop.meshAlphaModes)
	{
		if (mode == 2)
			return true;
	}

	if (prop.transparentAlpha < 255)
		return true;

	return false;
}
Matrix Game::GetScenePropColliderBaseMatrix(const SceneProp& prop) const
{
	// This is NOT the draw matrix.
	// This is the coordinate system used by physics:
	// body center = prop.position + rotated/scaled colliderOffset.
	return BuildModelMatrix(
		prop.position,
		prop.rotationDeg,
		prop.scale
	);
}
bool Game::GetScenePropModelBoundsInColliderLocal(
	const SceneProp& prop,
	BoundingBox& outBounds
) const
{
	if (prop.model == nullptr)
		return false;

	BoundingBox modelBounds = GetModelBoundingBox(*prop.model);

	// Must match how the prop is actually drawn.
	Matrix drawMatrix = GetScenePropDrawMatrix(prop);

	// Must match how the collider offset is interpreted.
	Matrix colliderBaseMatrix = GetScenePropColliderBaseMatrix(prop);
	Matrix colliderBaseInverse = MatrixInvert(colliderBaseMatrix);

	Vector3 corners[8] =
	{
		{ modelBounds.min.x, modelBounds.min.y, modelBounds.min.z },
		{ modelBounds.max.x, modelBounds.min.y, modelBounds.min.z },
		{ modelBounds.min.x, modelBounds.max.y, modelBounds.min.z },
		{ modelBounds.max.x, modelBounds.max.y, modelBounds.min.z },

		{ modelBounds.min.x, modelBounds.min.y, modelBounds.max.z },
		{ modelBounds.max.x, modelBounds.min.y, modelBounds.max.z },
		{ modelBounds.min.x, modelBounds.max.y, modelBounds.max.z },
		{ modelBounds.max.x, modelBounds.max.y, modelBounds.max.z }
	};

	bool hasBounds = false;

	for (int i = 0; i < 8; i++)
	{
		Vector3 worldPoint = Vector3Transform(corners[i], drawMatrix);
		Vector3 colliderLocalPoint = Vector3Transform(worldPoint, colliderBaseInverse);

		ExpandBoundsWithPoint(outBounds, colliderLocalPoint, hasBounds);
	}

	return hasBounds;
}

void Game::DrawSceneProps(const Camera3D& cam)
{
	debugGpuOccRejectNoQueryArray = 0;
	debugGpuOccRejectShouldTest = 0;
	debugGpuOccRejectPending = 0;
	debugGpuOccRejectNoBounds = 0;

	perf.scenePropsTotal = (int)sceneProps.size();
	perf.scenePropsDrawn = 0;
	perf.scenePropsCulled = 0;
	perf.scenePropsOcclusionCulled = 0;
	perf.gpuOcclusionQueriesIssued = 0;
	perf.gpuOcclusionCulled = 0;

	gpuOcclusionQueriesIssued = 0;
	gpuOcclusionCulled = 0;

	if (enableGpuOcclusionCulling)
	{
		EnsureScenePropOcclusionQueries();
		ReadScenePropOcclusionQueryResults();
	}

	visibleScenePropIndices.clear();
	visibleScenePropIndices.reserve(sceneProps.size());

	frustumVisibleScenePropIndices.clear();
	frustumVisibleScenePropIndices.reserve(sceneProps.size());
	frustumVisibleNowScratch.assign(sceneProps.size(), 0);
	occluderDrawnNowScratch.assign(sceneProps.size(), 0);

	std::vector<unsigned char>& frustumVisibleNow = frustumVisibleNowScratch;
	std::vector<unsigned char>& occluderDrawnNow = occluderDrawnNowScratch;
	// Pass 1: normal visibility/frustum/distance culling.
	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		if (hasHeldBody && i == heldScenePropIndex)
			continue;

		const SceneProp& prop = sceneProps[i];

		if (!ShouldDrawSceneProp(prop, cam))
		{
			perf.scenePropsCulled++;
			continue;
		}

		frustumVisibleNow[i] = 1;

		if (enableGpuOcclusionCulling &&
			i < (int)scenePropWasFrustumVisible.size() &&
			i < (int)scenePropOcclusionVisible.size() &&
			i < (int)scenePropOcclusionGraceFrames.size())
		{
			bool newlyEnteredFrustum = scenePropWasFrustumVisible[i] == 0;
			bool wasPreviouslyHiddenByGpu = scenePropOcclusionVisible[i] == 0;

			if (newlyEnteredFrustum &&
				wasPreviouslyHiddenByGpu &&
				ShouldGpuOcclusionTestSceneProp(i) &&
				ShouldGpuOcclusionQueryByScreenSize(i, cam))
			{
				// Draw briefly while the GPU query catches up.
				// Do NOT set scenePropOcclusionVisible[i] = 1 here,
				// otherwise hidden-priority queries will not re-test it quickly.
				scenePropOcclusionGraceFrames[i] = (unsigned char)gpuOcclusionRevealGraceFrames;
			}
		}

		frustumVisibleScenePropIndices.push_back(i);
	}
	/*
	int issuedThisFrame = 0;

	int visibleCount = (int)frustumVisibleScenePropIndices.size();

	if (visibleCount > 0)
	{
		gpuOcclusionCursor %= visibleCount;

		for (int offset = 0;
			offset < visibleCount && issuedThisFrame < maxGpuOcclusionQueriesPerFrame;
			offset++)
		{
			int listIndex = (gpuOcclusionCursor + offset) % visibleCount;
			int propIndex = frustumVisibleScenePropIndices[listIndex];

			if (!ShouldGpuOcclusionTestSceneProp(propIndex))
			{
				debugGpuOccRejectShouldTest++;
				continue;
			}

			if (!ShouldGpuOcclusionQueryByScreenSize(propIndex, cam))
				continue;

			IssueScenePropOcclusionQuery(propIndex);
			issuedThisFrame++;
		}

		gpuOcclusionCursor =
			(gpuOcclusionCursor + maxGpuOcclusionQueriesPerFrame) % visibleCount;
	}
	*/
	// Pass 2: draw occluders first.
	// These write to the depth buffer, so GPU queries can test against them.


	// Pass 2: draw occluders first.
// These write to the depth buffer, so GPU queries can test against them.
	for (int i : frustumVisibleScenePropIndices)
	{
		const SceneProp& prop = sceneProps[i];

		if (!prop.canOcclude)
			continue;

		if (!prop.visible || prop.model == nullptr)
			continue;

		// Do NOT skip just because it needs a transparent pass.
		// DrawScenePropByIndexPass(i, false) will draw only opaque/alpha-mask meshes.
		visibleScenePropIndices.push_back(i);

		perf.scenePropsDrawn++;
		DrawScenePropByIndexPass(i, false);

		occluderDrawnNow[i] = 1;
	}

	// Pass 3: issue invisible GPU occlusion queries.

	// Important: limit the number per frame. Querying everything can be slower than drawing everything.
	double queryStart = GetTime();

	if (enableGpuOcclusionCulling)
	{
		rlDrawRenderBatchActive();

		rlEnableDepthTest();
		rlDisableDepthMask();
		rlDisableBackfaceCulling();

		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

		int issuedThisFrame = 0;
		int visibleCount = (int)frustumVisibleScenePropIndices.size();

		auto TryIssueGpuOcclusionQuery = [&](int propIndex) -> bool
			{
				if (issuedThisFrame >= maxGpuOcclusionQueriesPerFrame)
					return false;

				if (!ShouldGpuOcclusionTestSceneProp(propIndex))
				{
					debugGpuOccRejectShouldTest++;
					return false;
				}

				if (!ShouldGpuOcclusionQueryByScreenSize(propIndex, cam))
					return false;

				IssueScenePropOcclusionQuery(propIndex);
				issuedThisFrame++;
				return true;
			};

		if (visibleCount > 0)
		{
			// Priority 1: query props that are currently believed to be hidden.
			// These are the ones most likely to pop in late around corners.
			for (int propIndex : frustumVisibleScenePropIndices)
			{
				if (issuedThisFrame >= maxGpuOcclusionQueriesPerFrame)
					break;

				if (propIndex >= 0 &&
					propIndex < (int)scenePropOcclusionVisible.size() &&
					scenePropOcclusionVisible[propIndex] == 0)
				{
					TryIssueGpuOcclusionQuery(propIndex);
				}
			}

			// Priority 2: normal round-robin refresh for the rest.
			if (issuedThisFrame < maxGpuOcclusionQueriesPerFrame)
			{
				gpuOcclusionCursor %= visibleCount;

				for (int offset = 0;
					offset < visibleCount && issuedThisFrame < maxGpuOcclusionQueriesPerFrame;
					offset++)
				{
					int listIndex = (gpuOcclusionCursor + offset) % visibleCount;
					int propIndex = frustumVisibleScenePropIndices[listIndex];

					TryIssueGpuOcclusionQuery(propIndex);
				}

				gpuOcclusionCursor =
					(gpuOcclusionCursor + maxGpuOcclusionQueriesPerFrame) % visibleCount;
			}
		}

		rlDrawRenderBatchActive();

		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

		rlEnableBackfaceCulling();
		rlEnableDepthMask();
	}

	perf.gpuOcclusionQueryMs = MsSince(queryStart);

	// Pass 4: draw normal props using previous-frame query result.
	for (int i : frustumVisibleScenePropIndices)
	{
		const SceneProp& prop = sceneProps[i];

		// Already drawn as occluder.
// Already drawn as an occluder this frame.
		if (i < (int)occluderDrawnNow.size() && occluderDrawnNow[i])
			continue;

		bool culledByGpuOcclusion = false;

		bool forceVisibleByGrace = false;

		if (i < (int)scenePropOcclusionGraceFrames.size() &&
			scenePropOcclusionGraceFrames[i] > 0)
		{
			scenePropOcclusionGraceFrames[i]--;
			forceVisibleByGrace = true;
		}

		if (!forceVisibleByGrace &&
			enableGpuOcclusionCulling &&
			ShouldGpuOcclusionTestSceneProp(i) &&
			ShouldGpuOcclusionQueryByScreenSize(i, cam) &&
			i < (int)scenePropOcclusionVisible.size() &&
			scenePropOcclusionVisible[i] == 0)
		{
			culledByGpuOcclusion = true;
		}

		if (culledByGpuOcclusion)
		{
			perf.scenePropsOcclusionCulled++;
			perf.scenePropsCulled++;
			perf.gpuOcclusionCulled++;
			gpuOcclusionCulled++;
			continue;
		}

		visibleScenePropIndices.push_back(i);

		perf.scenePropsDrawn++;
		DrawScenePropByIndexPass(i, false);
	}

	perf.gpuOcclusionQueriesIssued = gpuOcclusionQueriesIssued;

	// Transparent pass only for props that need it.
	BeginBlendMode(BLEND_ALPHA);
	rlDisableDepthMask();
	rlDisableBackfaceCulling();
	for (int i : visibleScenePropIndices)
	{
		const SceneProp& prop = sceneProps[i];

		if (!ScenePropNeedsTransparentPass(prop))
			continue;

		DrawScenePropByIndexPass(i, true);
	}

	rlEnableBackfaceCulling();
	rlEnableDepthMask();
	EndBlendMode();

	if (enableGpuOcclusionCulling &&
		scenePropWasFrustumVisible.size() == frustumVisibleNow.size())
	{
		scenePropWasFrustumVisible = frustumVisibleNow;
	}
}
bool Game::ShouldGpuOcclusionQueryByScreenSize(
	int propIndex,
	const Camera3D& cam
) const
{
	Rectangle rect{};
	float depth = 0.0f;

	if (!GetScenePropScreenRect(propIndex, cam, rect, depth))
		return false;

	float area = rect.width * rect.height;

	return area >= minGpuOcclusionQueryScreenArea;
}

void Game::IssueScenePropOcclusionQuery(int propIndex)
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return;

	if (propIndex >= (int)scenePropOcclusionQueries.size())
	{
		debugGpuOccRejectNoQueryArray++;
		return;
	}

	if (scenePropOcclusionPending[propIndex])
	{
		debugGpuOccRejectPending++;
		return;
	}

	const SceneProp& prop = sceneProps[propIndex];

	BoundingBox bounds{};

	if (!GetScenePropRenderBoundsWorld(prop, bounds))
	{
		debugGpuOccRejectNoBounds++;
		return;
	}

	Vector3 center = {
		(bounds.min.x + bounds.max.x) * 0.5f,
		(bounds.min.y + bounds.max.y) * 0.5f,
		(bounds.min.z + bounds.max.z) * 0.5f
	};

	Vector3 size = {
		bounds.max.x - bounds.min.x,
		bounds.max.y - bounds.min.y,
		bounds.max.z - bounds.min.z
	};

	if (size.x <= 0.01f || size.y <= 0.01f || size.z <= 0.01f)
	{
		debugGpuOccRejectNoBounds++;
		return;
	}

	GLuint query = scenePropOcclusionQueries[propIndex];

	glBeginQuery(GL_ANY_SAMPLES_PASSED_CONSERVATIVE, query);

	DrawCubeV(center, size, WHITE);

	// Required because DrawCubeV is raylib-batched.
	// The query needs the cube to actually be submitted before glEndQuery().
	rlDrawRenderBatchActive();

	glEndQuery(GL_ANY_SAMPLES_PASSED_CONSERVATIVE);

	scenePropOcclusionPending[propIndex] = 1;
	gpuOcclusionQueriesIssued++;
}
bool Game::ShouldGpuOcclusionTestSceneProp(int propIndex) const
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return false;

	const SceneProp& prop = sceneProps[propIndex];

	if (!prop.visible)
		return false;

	if (prop.model == nullptr)
		return false;

	if (prop.ignoreOcclusionCulling)
		return false;

	// Do not test occluders themselves.
	if (prop.canOcclude)
		return false;

	// TEMP: do not reject transparent props yet.
	// Some imported GLB objects may be incorrectly classified.
	// if (ScenePropNeedsTransparentPass(prop))
	//     return false;

	return true;
}
float Game::GetYawToTargetXZ(Vector3 from, Vector3 to) const
{
	Vector3 dir = Vector3Subtract(to, from);
	dir.y = 0.0f;

	if (Vector3Length(dir) <= 0.001f)
		return 0.0f;

	dir = Vector3Normalize(dir);

	return atan2f(dir.x, dir.z) * RAD2DEG;
}

void Game::SetCustomerYawTowards(int customerIndex, Vector3 target)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	Customer& customer = customers[customerIndex];
	customer.yawDeg = GetYawToTargetXZ(customer.position, target);
}

void Game::ApplyPOIFacingToCustomer(int customerIndex, int poiIndex)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	if (poiIndex < 0 || poiIndex >= (int)customerPOIs.size())
		return;

	const CustomerPOI& poi = customerPOIs[poiIndex];
	Customer& customer = customers[customerIndex];

	if (poi.useFacingDirection)
	{
		customer.yawDeg = poi.facingYawDeg;
	}
}

void Game::ReadScenePropOcclusionQueryResults()
{
	for (int i = 0; i < (int)scenePropOcclusionQueries.size(); i++)
	{
		if (!scenePropOcclusionPending[i])
			continue;

		GLuint available = 0;

		glGetQueryObjectuiv(
			scenePropOcclusionQueries[i],
			GL_QUERY_RESULT_AVAILABLE,
			&available
		);

		if (!available)
			continue;

		GLuint visible = 1;

		glGetQueryObjectuiv(
			scenePropOcclusionQueries[i],
			GL_QUERY_RESULT,
			&visible
		);

		scenePropOcclusionVisible[i] = visible ? 1 : 0;
		scenePropOcclusionPending[i] = 0;
	}
}

void Game::EnsureScenePropOcclusionQueries()
{
	int count = (int)sceneProps.size();

	if ((int)scenePropOcclusionQueries.size() == count)
		return;

	if (!scenePropOcclusionQueries.empty())
	{
		glDeleteQueries(
			(GLsizei)scenePropOcclusionQueries.size(),
			scenePropOcclusionQueries.data()
		);
	}

	scenePropOcclusionQueries.clear();
	scenePropOcclusionVisible.clear();
	scenePropOcclusionPending.clear();

	scenePropOcclusionQueries.resize(count, 0);
	scenePropOcclusionVisible.resize(count, 1);
	scenePropOcclusionPending.resize(count, 0);
	scenePropWasFrustumVisible.resize(count, 0);
	scenePropOcclusionGraceFrames.resize(count, 0);
	if (count > 0)
	{
		glGenQueries(
			count,
			scenePropOcclusionQueries.data()
		);
	}
}

void Game::UnloadScenePropOcclusionQueries()
{
	if (!scenePropOcclusionQueries.empty())
	{
		glDeleteQueries(
			(GLsizei)scenePropOcclusionQueries.size(),
			scenePropOcclusionQueries.data()
		);
	}

	scenePropOcclusionQueries.clear();
	scenePropOcclusionVisible.clear();
	scenePropOcclusionPending.clear();
}

static Vector3 QuaternionToEulerDegSafe(Quaternion q)
{
	q = QuaternionNormalize(q);

	Vector3 e = QuaternionToEuler(q);

	return {
		e.x * RAD2DEG,
		e.y * RAD2DEG,
		e.z * RAD2DEG
	};
}

bool Game::LoadSfx(
	const std::string& id,
	const std::string& path,
	float volume
)
{
	if (id.empty() || path.empty())
		return false;

	if (!FileExists(path.c_str()))
	{
		TraceLog(
			LOG_WARNING,
			"SFX file missing: id=%s path=%s",
			id.c_str(),
			path.c_str()
		);

		return false;
	}

	auto existing = sfx.find(id);

	if (existing != sfx.end())
	{
		UnloadSound(existing->second);
		sfx.erase(existing);
	}

	Sound sound = LoadSound(path.c_str());

	if (sound.frameCount == 0)
	{
		TraceLog(
			LOG_WARNING,
			"Failed to load SFX: id=%s path=%s",
			id.c_str(),
			path.c_str()
		);

		return false;
	}

	SetSoundVolume(sound, volume);

	sfx[id] = sound;

	sfxBaseVolumes[id] = volume;

	TraceLog(
		LOG_INFO,
		"Loaded SFX: id=%s path=%s",
		id.c_str(),
		path.c_str()
	);

	return true;
}

void Game::LoadSoundEffects()
{
	// Add/change sound paths here only.
	LoadSfx("scan", "Audio/Soundfx/scan.mp3", 0.85f);

	// Cash register / transaction sound
	LoadSfx("cash", "Audio/Soundfx/cash.mp3", 0.85f);

	LoadSfx("amongus", "Audio/Soundfx/amongus.mp3", 1.0f);

	LoadSfx("ultrakillHA", "Audio/Soundfx/ultrakillHA.mp3", 0.85f);

	// Store / day flow sounds
	LoadSfx("store_open", "Audio/Soundfx/openclose.wav", 0.85f);
	LoadSfx("store_close", "Audio/Soundfx/store_close.mp3", 0.85f);
	LoadSfx("day_result", "Audio/Soundfx/cheer.mp3", 0.90f);

	LoadSfx("dialogue_beep", "Audio/Soundfx/sans-talking-short.mp3", 0.45f);
	LoadSfx("cat_purr", "Audio/Soundfx/catpurr.wav", 0.7f);
	LoadSfx("forehead", "Audio/Soundfx/grom_kill_vo_04.ogg", 0.7f);
	LoadSfx("music_box", "Audio/Soundfx/music_box.ogg", 0.8f);
	LoadSfx("invisible", "Audio/Soundfx/invisible.mp3", 0.8f);
}

void Game::UnloadSoundEffects()
{
	for (auto& pair : sfx)
	{
		UnloadSound(pair.second);
	}

	sfx.clear();
	sfxBaseVolumes.clear();
}

void Game::PlayLoopingSfx(
	const std::string& id,
	bool restartIfDifferent
)
{
	if (id.empty())
		return;

	auto it = sfx.find(id);

	if (it == sfx.end())
	{
		TraceLog(
			LOG_WARNING,
			"Looping SFX not found: %s",
			id.c_str()
		);

		return;
	}

	if (loopingSfxActive && activeLoopingSfxId == id)
	{
		Sound& currentSound = it->second;

		if (!IsSoundPlaying(currentSound))
		{
			PlaySound(currentSound);
		}

		return;
	}

	if (loopingSfxActive && restartIfDifferent)
	{
		StopLoopingSfx();
	}

	activeLoopingSfxId = id;
	loopingSfxActive = true;

	Sound& sound = it->second;

	if (IsSoundPlaying(sound))
	{
		StopSound(sound);
	}

	PlaySound(sound);
}

void Game::StopLoopingSfx(const std::string& id)
{
	if (!loopingSfxActive)
		return;

	if (!id.empty() && activeLoopingSfxId != id)
		return;

	auto it = sfx.find(activeLoopingSfxId);

	if (it != sfx.end())
	{
		Sound& sound = it->second;

		if (IsSoundPlaying(sound))
		{
			StopSound(sound);
		}
	}

	activeLoopingSfxId.clear();
	loopingSfxActive = false;
}

void Game::UpdateLoopingSfx()
{
	if (!loopingSfxActive)
		return;

	auto it = sfx.find(activeLoopingSfxId);

	if (it == sfx.end())
	{
		activeLoopingSfxId.clear();
		loopingSfxActive = false;
		return;
	}

	Sound& sound = it->second;

	if (!IsSoundPlaying(sound))
	{
		PlaySound(sound);
	}
}

bool Game::ShouldInspectSfxLoopForTag(const std::string& tag) const
{
	std::string cleanTag = SanitizeVoiceToken(tag);

	if (cleanTag == "ultrakillha")
		return true;

	if (cleanTag == "music_box")
		return true;

	return false;
}

void Game::PlaySfx(
	const std::string& id,
	bool restartIfPlaying
)
{
	auto it = sfx.find(id);

	if (it == sfx.end())
	{
		TraceLog(
			LOG_WARNING,
			"SFX not found: %s",
			id.c_str()
		);

		return;
	}

	Sound& sound = it->second;

	if (restartIfPlaying && IsSoundPlaying(sound))
	{
		StopSound(sound);
	}

	PlaySound(sound);
}

int Game::FindBestCounterOfferReturnSpotForProp(int propIndex) const
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return -1;

	const SceneProp& prop = sceneProps[propIndex];

	Vector3 nearPosition = prop.position;

	if (physics && !prop.bodyId.IsInvalid())
	{
		nearPosition = physics->GetBodyPosition(prop.bodyId);
	}

	int bestIndex = -1;
	float bestScore = FLT_MAX;

	for (int i = 0; i < (int)itemPlacementSpots.size(); i++)
	{
		const ItemPlacementSpot& spot = itemPlacementSpots[i];

		if (!spot.enabled)
			continue;

		if (!spot.allowPlayerDrop)
			continue;

		if (spot.kind != ItemPlacementSpotKind::CounterOffer)
			continue;

		if (IsPlacementSpotOccupiedByValidProp(i, propIndex))
			continue;

		float itemDist = Vector3Distance(nearPosition, spot.position);
		float playerDist = Vector3Distance(player.m_pos, spot.position);

		float score = fminf(itemDist, playerDist * 0.85f);

		// Slightly more forgiving than normal shelf placement.
		float radius = fmaxf(spot.snapRadius, 0.85f);

		if (score > radius)
			continue;

		if (score < bestScore)
		{
			bestScore = score;
			bestIndex = i;
		}
	}

	return bestIndex;
}

bool Game::IsScannerSpot(int spotIndex) const
{
	if (spotIndex < 0 || spotIndex >= (int)itemPlacementSpots.size())
		return false;

	return itemPlacementSpots[spotIndex].kind == ItemPlacementSpotKind::CounterScan;
}

void Game::CompleteBuyerReturnAfterScan(int customerIndex)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	Customer& customer = customers[customerIndex];

	if (customer.role != CustomerRole::Browser)
		return;

	// Apply budget only once.
	if (!customer.transactionApplied)
	{
		ApplyBuyerPurchaseTransaction(customerIndex);
	}

	// Buyer takes back the returned item.
	CustomerTakeCounterItem(customerIndex);

	customer.waitingForPlayerToScanItem = false;
	customer.waitingForPlayerToReturnScannedItem = false;
	customer.waitingForPlayerToTakeCounterItem = false;
	customer.counterServiceCompleted = true;

	customer.hasMoveTarget = false;
	customer.pathWaypoints.clear();
	customer.pathWaypointCursor = 0;

	customer.SetAnimState(CustomerAnimState::Give);
	customer.aiState = CustomerAIState::BrowserPurchasing;
	customer.poiWaitTimer = 0.15f;

	RebuildItemPlacementSpotOccupancy();
	if (dialogueActive && activeDialogueCustomerIndex == customerIndex)
	{
		CloseDialogue();
	}
	AdvanceQueueLine();
}

void Game::OnScenePropPlacedAtSpot(int propIndex, int spotIndex)
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return;

	if (spotIndex < 0 || spotIndex >= (int)itemPlacementSpots.size())
		return;

	SceneProp& prop = sceneProps[propIndex];
	const ItemPlacementSpot& spot = itemPlacementSpots[spotIndex];

	if (spot.kind != ItemPlacementSpotKind::CounterOffer)
		return;

	if (!prop.placedByCustomer)
		return;

	if (!prop.scannedForCustomer)
		return;

	int customerIndex = ResolveCustomerOwnerForCounterProp(propIndex);

	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	Customer& customer = customers[customerIndex];

	if (customer.role != CustomerRole::Browser)
		return;

	if (customer.counterItemScenePropIndex != propIndex)
		return;

	if (!customer.waitingForPlayerToReturnScannedItem)
		return;

	prop.owningCustomerIndex = customerIndex;

	CompleteBuyerReturnAfterScan(customerIndex);
}

bool Game::TryReturnHeldScannedCustomerItemToCounter()
{
	if (!hasHeldBody)
		return false;

	if (heldItemScanState != HeldItemScanState::None)
		return false;

	if (heldScenePropIndex < 0 ||
		heldScenePropIndex >= (int)sceneProps.size())
	{
		return false;
	}

	SceneProp& prop = sceneProps[heldScenePropIndex];

	if (!prop.placedByCustomer)
		return false;

	if (!prop.scannedForCustomer)
		return false;


	int customerIndex = ResolveCustomerOwnerForCounterProp(heldScenePropIndex);

	if (customerIndex >= 0 && customerIndex < (int)customers.size())
	{
		prop.owningCustomerIndex = customerIndex;
		customers[customerIndex].counterItemScenePropIndex = heldScenePropIndex;
	}

	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return false;

	Customer& customer = customers[customerIndex];

	if (customer.role != CustomerRole::Browser)
		return false;

	if (customer.counterItemScenePropIndex != heldScenePropIndex)
		return false;

	if (!customer.waitingForPlayerToReturnScannedItem)
		return false;

	int spotIndex = FindBestCounterOfferReturnSpotForProp(heldScenePropIndex);

	if (spotIndex < 0)
	{
		lastTransactionDeltaYen = 0;
		lastTransactionText = "Return the scanned item to the counter.";
		return false;
	}

	int returnedPropIndex = heldScenePropIndex;

	bool placed = PlaceScenePropAtSpot(
		returnedPropIndex,
		spotIndex,
		true
	);

	if (!placed)
	{
		lastTransactionDeltaYen = 0;
		lastTransactionText = "Counter spot is blocked.";
		return false;
	}

	// PlaceScenePropAtSpot(...) calls OnScenePropPlacedAtSpot(...),
	// which calls CompleteBuyerReturnAfterScan(...),
	// which calls CustomerTakeCounterItem(...).
	ClearHeldStateAfterCustomerTakesItem(returnedPropIndex);

	return true;
}

bool Game::TryStartHeldItemScanFromCurrentTarget(int propIndex)
{
	if (!hasHeldBody)
		return false;

	if (heldItemScanState != HeldItemScanState::None)
		return false;

	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return false;

	const SceneProp& prop = sceneProps[propIndex];

	if (!prop.canPickup)
		return false;

	if (prop.owningCustomerIndex >= 0 &&
		prop.owningCustomerIndex < (int)customers.size())
	{
		const Customer& owner = customers[prop.owningCustomerIndex];

		if (owner.role == CustomerRole::Browser &&
			prop.scannedForCustomer &&
			owner.waitingForPlayerToReturnScannedItem)
		{
			lastTransactionDeltaYen = 0;
			lastTransactionText = "Already scanned. Return item to counter.";
			return false;
		}
	}

	int spotIndex = -1;

	// First priority: currently aimed placement preview.
	if (placementPreviewValid &&
		placementPreviewPropIndex == propIndex &&
		targetedPlacementSpotIndex >= 0 &&
		IsScannerSpot(targetedPlacementSpotIndex))
	{
		spotIndex = targetedPlacementSpotIndex;
	}

	// Second priority: screen-center ray.
	if (spotIndex < 0)
	{
		Vector2 screenCenter = {
			(float)GetScreenWidth() * 0.5f,
			(float)GetScreenHeight() * 0.5f
		};

		Ray centerRay = GetScreenToWorldRay(screenCenter, camera);

		int raySpotIndex = FindBestPlacementSpotForPropFromRay(
			propIndex,
			centerRay,
			3.0f
		);

		if (IsScannerSpot(raySpotIndex))
		{
			spotIndex = raySpotIndex;
		}
	}

	if (spotIndex < 0)
		return false;

	return StartHeldItemScan(propIndex, spotIndex);
}

void Game::ComputeHeldItemTargetTransform(
	Vector3& outPos,
	Quaternion& outRot
) const
{
	outPos = camera.position;
	outRot = QuaternionIdentity();

	if (!hasHeldBody)
		return;

	Vector3 holdOffsetLocal{ 0.0f, -0.2f, 0.0f };
	Vector3 holdRotationOffsetDeg{ 0.0f, 0.0f, 0.0f };
	bool followCameraPitch = false;

	if (heldScenePropIndex >= 0 &&
		heldScenePropIndex < (int)sceneProps.size())
	{
		const SceneProp& prop = sceneProps[heldScenePropIndex];

		holdOffsetLocal = prop.holdOffsetLocal;
		holdRotationOffsetDeg = prop.holdRotationOffsetDeg;
		followCameraPitch = prop.holdFollowCameraPitch;
	}

	Vector3 camForward = Vector3Normalize(
		Vector3Subtract(camera.target, camera.position)
	);

	Vector3 camRight = Vector3Normalize(
		Vector3CrossProduct(camForward, camera.up)
	);

	Vector3 camUp = Vector3Normalize(
		Vector3CrossProduct(camRight, camForward)
	);

	float camYawDeg = atan2f(camForward.x, camForward.z) * RAD2DEG;

	Quaternion qYaw = QuaternionFromAxisAngle(
		{ 0.0f, 1.0f, 0.0f },
		camYawDeg * DEG2RAD
	);

	Quaternion qBase = qYaw;

	if (followCameraPitch)
	{
		float camPitchDeg = asinf(Clamp(camForward.y, -1.0f, 1.0f)) * RAD2DEG;

		Quaternion qPitch = QuaternionFromAxisAngle(
			camRight,
			-camPitchDeg * DEG2RAD
		);

		qBase = QuaternionMultiply(qPitch, qYaw);
	}

	Quaternion qOffset = QuaternionFromEuler(
		holdRotationOffsetDeg.x * DEG2RAD,
		holdRotationOffsetDeg.y * DEG2RAD,
		holdRotationOffsetDeg.z * DEG2RAD
	);

	outRot = QuaternionNormalize(
		QuaternionMultiply(qBase, qOffset)
	);

	float distanceFromCamera = holdDistance + holdOffsetLocal.z;

	outPos = camera.position;
	outPos = Vector3Add(outPos, Vector3Scale(camForward, distanceFromCamera));
	outPos = Vector3Add(outPos, Vector3Scale(camRight, holdOffsetLocal.x));
	outPos = Vector3Add(outPos, Vector3Scale(camUp, holdOffsetLocal.y));
}

bool Game::StartHeldItemScan(int propIndex, int spotIndex)
{
	if (!hasHeldBody)
		return false;

	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return false;

	if (!IsScannerSpot(spotIndex))
		return false;

	if (heldItemScanState != HeldItemScanState::None)
		return false;

	if (!physics || heldBody.IsInvalid())
		return false;

	Vector3 previewPos{};
	Vector3 previewRotDeg{};

	ComputePlacementPreviewForSpot(
		propIndex,
		spotIndex,
		previewPos,
		previewRotDeg
	);

	scanningScenePropIndex = propIndex;
	scanningSpotIndex = spotIndex;

	scanStartPos = physics->GetBodyPosition(heldBody);
	scanStartRot = physics->GetBodyRotation(heldBody);

	const SceneProp& prop = sceneProps[propIndex];

	scanTargetPos = GetScenePropBodyCenterForPose(
		prop,
		previewPos,
		previewRotDeg
	);

	scanTargetRot = QuaternionFromEuler(
		previewRotDeg.x * DEG2RAD,
		previewRotDeg.y * DEG2RAD,
		previewRotDeg.z * DEG2RAD
	);

	scanTimer = 0.0f;
	heldItemScanState = HeldItemScanState::MovingToScanner;

	return true;
}

void Game::MarkHeldItemScanned(int propIndex)
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return;

	SceneProp& prop = sceneProps[propIndex];

	// Default: a scan does NOT mean this is a buyer-return item.
	prop.scannedForCustomer = false;
	prop.scannedPriceYen = 0;

	int customerIndex = ResolveCustomerOwnerForCounterProp(propIndex);

	if (customerIndex >= 0 && customerIndex < (int)customers.size())
	{
		Customer& customer = customers[customerIndex];

		if (customer.role == CustomerRole::Browser &&
			customer.waitingForPlayerToScanItem &&
			customer.counterItemScenePropIndex == propIndex)
		{
			const CustomerTradeItemDef* item = FindCustomerTradeItemDef(
				customer.tradeItemId
			);

			if (item != nullptr)
			{
				prop.scannedForCustomer = true;
				prop.owningCustomerIndex = customerIndex;
				prop.scannedPriceYen = GetBuyerSellPriceYen(*item);

				customer.waitingForPlayerToScanItem = false;
				customer.waitingForPlayerToReturnScannedItem = true;

				lastTransactionDeltaYen = 0;
				lastTransactionText = TextFormat(
					"Scanned: \xC2\xA5%i. Return item to counter.",
					prop.scannedPriceYen
				);

				return;
			}
		}
	}

	// Seller customer item: do not treat scanner as buyer checkout.
	if (prop.owningCustomerIndex >= 0 &&
		prop.owningCustomerIndex < (int)customers.size())
	{
		const Customer& owner = customers[prop.owningCustomerIndex];

		if (owner.role == CustomerRole::Seller)
		{
			lastTransactionDeltaYen = 0;
			lastTransactionText = "Seller items are appraised through dialogue, not scanner checkout.";
			return;
		}
	}

	// Generic non-customer scan.
	lastTransactionDeltaYen = 0;
	lastTransactionText = "Scanned item.";
}
void Game::SyncScanningScenePropFromBody()
{
	if (scanningScenePropIndex < 0 ||
		scanningScenePropIndex >= (int)sceneProps.size())
	{
		return;
	}

	if (!physics)
		return;

	SceneProp& prop = sceneProps[scanningScenePropIndex];

	if (prop.bodyId.IsInvalid())
		return;

	ReadScenePropTransformFromBody(prop);

	if (prop.importedFromGlbScene && prop.model != nullptr)
	{
		SyncImportedEditorOffsetFromRuntime(prop);
	}
	else if (prop.parentIndex == -1)
	{
		SyncScenePropLocalFromWorld(prop);
	}
}

void Game::UpdateHeldItemScan(float dt)
{
	if (heldItemScanState == HeldItemScanState::None)
		return;

	if (!hasHeldBody || !physics || heldBody.IsInvalid())
	{
		heldItemScanState = HeldItemScanState::None;
		return;
	}

	auto MoveHeldBodyAndSync = [&](Vector3 pos, Quaternion rot)
		{
			physics->SetBodyPosition(heldBody, pos);
			physics->SetBodyRotation(heldBody, rot);
			physics->SetBodyLinearVelocity(heldBody, { 0.0f, 0.0f, 0.0f });
			physics->SetBodyAngularVelocity(heldBody, { 0.0f, 0.0f, 0.0f });

			SyncScanningScenePropFromBody();
		};

	auto LerpWithArc = [](Vector3 a, Vector3 b, float t, float arcHeight)
		{
			Vector3 p = Vector3Lerp(a, b, t);
			p.y += sinf(t * PI) * arcHeight;
			return p;
		};

	scanTimer += dt;

	const float arcHeight = 0.22f;

	if (heldItemScanState == HeldItemScanState::MovingToScanner)
	{
		float t = Clamp(scanTimer / scanMoveDuration, 0.0f, 1.0f);
		float smoothT = t * t * (3.0f - 2.0f * t);

		Vector3 pos = LerpWithArc(
			scanStartPos,
			scanTargetPos,
			smoothT,
			arcHeight
		);

		Quaternion rot = QuaternionNormalize(
			QuaternionSlerp(scanStartRot, scanTargetRot, smoothT)
		);

		MoveHeldBodyAndSync(pos, rot);

		if (t >= 1.0f)
		{
			PlaySfx("scan");

			MarkHeldItemScanned(scanningScenePropIndex);

			scanTimer = 0.0f;
			heldItemScanState = HeldItemScanState::HoldAtScanner;
		}

		return;
	}

	if (heldItemScanState == HeldItemScanState::HoldAtScanner)
	{
		MoveHeldBodyAndSync(scanTargetPos, scanTargetRot);

		if (scanTimer >= scanHoldDuration)
		{
			scanStartPos = scanTargetPos;
			scanStartRot = scanTargetRot;
			scanTimer = 0.0f;
			heldItemScanState = HeldItemScanState::ReturningToHand;
		}

		return;
	}

	if (heldItemScanState == HeldItemScanState::ReturningToHand)
	{
		Vector3 handPos{};
		Quaternion handRot{};

		ComputeHeldItemTargetTransform(handPos, handRot);

		float t = Clamp(scanTimer / scanMoveDuration, 0.0f, 1.0f);
		float smoothT = t * t * (3.0f - 2.0f * t);

		Vector3 pos = LerpWithArc(
			scanStartPos,
			handPos,
			smoothT,
			arcHeight
		);

		Quaternion rot = QuaternionNormalize(
			QuaternionSlerp(scanStartRot, handRot, smoothT)
		);

		MoveHeldBodyAndSync(pos, rot);

		if (t >= 1.0f)
		{
			heldItemScanState = HeldItemScanState::None;
			scanningScenePropIndex = -1;
			scanningSpotIndex = -1;
			scanTimer = 0.0f;

			// Snap exactly back to normal held position on the final frame.
			UpdateHeldBody();
		}

		return;
	}
}

Vector3 Game::GetScenePropBodyCenterForPose(
	const SceneProp& prop,
	Vector3 propPosition,
	Vector3 propRotationDeg
) const
{
	Quaternion q = QuaternionFromEuler(
		propRotationDeg.x * DEG2RAD,
		propRotationDeg.y * DEG2RAD,
		propRotationDeg.z * DEG2RAD
	);

	Vector3 scaledOffset = GetScaledColliderOffset(prop);
	Vector3 rotatedOffset = Vector3RotateByQuaternion(scaledOffset, q);

	return Vector3Add(propPosition, rotatedOffset);
}

Vector3 Game::GetScenePropRotationForPlacementSpot(
	const SceneProp& prop,
	const ItemPlacementSpot& spot
) const
{
	Quaternion qSpot = QuaternionFromEuler(
		spot.rotationDeg.x * DEG2RAD,
		spot.rotationDeg.y * DEG2RAD,
		spot.rotationDeg.z * DEG2RAD
	);

	// Placement/counter/scanner correction should use dropRotationOffsetDeg,
	// not holdRotationOffsetDeg.
	Quaternion qPlacementOffset = QuaternionFromEuler(
		prop.dropRotationOffsetDeg.x * DEG2RAD,
		prop.dropRotationOffsetDeg.y * DEG2RAD,
		prop.dropRotationOffsetDeg.z * DEG2RAD
	);

	// final = spot orientation * item placement correction
	Quaternion qFinal = QuaternionNormalize(
		QuaternionMultiply(qSpot, qPlacementOffset)
	);

	return QuaternionToEulerDegSafe(qFinal);
}

Vector3 Game::GetScenePropPositionForPlacementSpot(
	const SceneProp& prop,
	const ItemPlacementSpot& spot,
	Vector3 finalRotationDeg
) const
{
	Quaternion q = QuaternionFromEuler(
		finalRotationDeg.x * DEG2RAD,
		finalRotationDeg.y * DEG2RAD,
		finalRotationDeg.z * DEG2RAD
	);

	Vector3 scaledOffset = GetScaledColliderOffset(prop);

	Vector3 half = {
		fabsf(prop.colliderSize.x * prop.scale.x) * 0.5f,
		fabsf(prop.colliderSize.y * prop.scale.y) * 0.5f,
		fabsf(prop.colliderSize.z * prop.scale.z) * 0.5f
	};

	Vector3 corners[8] =
	{
		{ scaledOffset.x - half.x, scaledOffset.y - half.y, scaledOffset.z - half.z },
		{ scaledOffset.x + half.x, scaledOffset.y - half.y, scaledOffset.z - half.z },
		{ scaledOffset.x - half.x, scaledOffset.y + half.y, scaledOffset.z - half.z },
		{ scaledOffset.x + half.x, scaledOffset.y + half.y, scaledOffset.z - half.z },

		{ scaledOffset.x - half.x, scaledOffset.y - half.y, scaledOffset.z + half.z },
		{ scaledOffset.x + half.x, scaledOffset.y - half.y, scaledOffset.z + half.z },
		{ scaledOffset.x - half.x, scaledOffset.y + half.y, scaledOffset.z + half.z },
		{ scaledOffset.x + half.x, scaledOffset.y + half.y, scaledOffset.z + half.z }
	};

	float minY = FLT_MAX;

	for (int i = 0; i < 8; i++)
	{
		Vector3 rotatedCorner = Vector3RotateByQuaternion(corners[i], q);
		minY = fminf(minY, rotatedCorner.y);
	}

	Vector3 rotatedCenterOffset = Vector3RotateByQuaternion(
		scaledOffset,
		q
	);

	Vector3 result = spot.position;

	// Align collider center X/Z to spot.
	result.x -= rotatedCenterOffset.x;
	result.z -= rotatedCenterOffset.z;

	// Align collider bottom to spot Y.
	result.y -= minY;

	return result;
}

void Game::CaptureLiveScenePropPhysicsTransforms()
{
	if (!physics)
		return;

	for (SceneProp& prop : sceneProps)
	{
		if (prop.bodyId.IsInvalid())
			continue;

		if (!prop.simulatePhysics)
			continue;

		// Read the real current Jolt body transform first.
		ReadScenePropTransformFromBody(prop);

		// Keep local transform in sync so UpdateScenePropWorldTransforms()
		// does not snap it back to the old saved/imported local position.
		SyncScenePropLocalFromWorld(prop);
	}
}

RenderTexture2D* Game::RenderBloomToTarget()
{
	if (!bloomEnabled)
		return nullptr;

	if (worldTarget.id == 0 ||
		bloomExtractTarget.id == 0 ||
		bloomPingTarget.id == 0 ||
		bloomPongTarget.id == 0 ||
		brightExtractShader.id == 0 ||
		blurShader.id == 0)
	{
		return nullptr;
	}

	Rectangle worldSrc = {
		0.0f,
		0.0f,
		(float)worldTarget.texture.width,
		-(float)worldTarget.texture.height
	};

	Rectangle bloomDst = {
		0.0f,
		0.0f,
		(float)bloomExtractTarget.texture.width,
		(float)bloomExtractTarget.texture.height
	};

	// 1. Extract bright pixels.
	BeginTextureMode(bloomExtractTarget);
	ClearBackground(BLANK);

	BeginShaderMode(brightExtractShader);
	SetShaderValue(brightExtractShader, brightThresholdLoc, &bloomThreshold, SHADER_UNIFORM_FLOAT);
	SetShaderValue(brightExtractShader, brightKneeLoc, &bloomKnee, SHADER_UNIFORM_FLOAT);

	DrawTexturePro(
		worldTarget.texture,
		worldSrc,
		bloomDst,
		{ 0.0f, 0.0f },
		0.0f,
		WHITE
	);

	EndShaderMode();
	EndTextureMode();

	// 2. Blur extracted bright pixels.
	RenderTexture2D* input = &bloomExtractTarget;
	RenderTexture2D* output = &bloomPingTarget;

	for (int i = 0; i < bloomBlurPasses; i++)
	{
		// Horizontal blur.
		{
			Rectangle src = {
				0.0f,
				0.0f,
				(float)input->texture.width,
				-(float)input->texture.height
			};

			Rectangle dst = {
				0.0f,
				0.0f,
				(float)output->texture.width,
				(float)output->texture.height
			};

			Vector2 texelSize = {
				1.0f / (float)input->texture.width,
				1.0f / (float)input->texture.height
			};

			BeginTextureMode(*output);
			ClearBackground(BLANK);

			BeginShaderMode(blurShader);

			Vector2 dir = { 1.0f, 0.0f };
			SetShaderValue(blurShader, blurDirectionLoc, &dir, SHADER_UNIFORM_VEC2);
			SetShaderValue(blurShader, blurTexelSizeLoc, &texelSize, SHADER_UNIFORM_VEC2);

			DrawTexturePro(input->texture, src, dst, { 0.0f, 0.0f }, 0.0f, WHITE);

			EndShaderMode();
			EndTextureMode();
		}

		input = &bloomPingTarget;
		output = &bloomPongTarget;

		// Vertical blur.
		{
			Rectangle src = {
				0.0f,
				0.0f,
				(float)input->texture.width,
				-(float)input->texture.height
			};

			Rectangle dst = {
				0.0f,
				0.0f,
				(float)output->texture.width,
				(float)output->texture.height
			};

			Vector2 texelSize = {
				1.0f / (float)input->texture.width,
				1.0f / (float)input->texture.height
			};

			BeginTextureMode(*output);
			ClearBackground(BLANK);

			BeginShaderMode(blurShader);

			Vector2 dir = { 0.0f, 1.0f };
			SetShaderValue(blurShader, blurDirectionLoc, &dir, SHADER_UNIFORM_VEC2);
			SetShaderValue(blurShader, blurTexelSizeLoc, &texelSize, SHADER_UNIFORM_VEC2);

			DrawTexturePro(input->texture, src, dst, { 0.0f, 0.0f }, 0.0f, WHITE);

			EndShaderMode();
			EndTextureMode();
		}

		input = &bloomPongTarget;
		output = &bloomPingTarget;
	}

	return input;
}

void Game::DrawHeldPropToTarget(const Camera3D& cam)
{
	if (!hasHeldBody || heldScenePropIndex < 0 || heldScenePropIndex >= (int)sceneProps.size())
		return;

	BeginTextureMode(heldPropTarget);
	ClearBackground(BLANK);

	BeginMode3D(cam);

	if (inspectMode)
	{
		UploadInspectionLightingToShader(cam);

		// Important: inspect-held props should not sample world shadow maps.
		DisableShadowsForShader(pbrShader);
	}
	else
	{
		UploadWorldLightingToShader();
	}

	DrawScenePropByIndex(heldScenePropIndex);

	UploadWorldLightingToShader();

	EndMode3D();
	EndTextureMode();
}

void Game::ComputeGachaInstanceAutoCollider()
{
	if (!gachaInstanceBatch.loaded)
		return;

	GetModelLocalBoundsCollider(
		&gachaInstanceBatch.model,
		gachaInstanceColliderSize,
		gachaInstanceColliderOffset
	);

	gachaInstanceColliderReady = true;

	TraceLog(
		LOG_INFO,
		"Gacha instance collider computed: size=(%.3f %.3f %.3f), offset=(%.3f %.3f %.3f)",
		gachaInstanceColliderSize.x,
		gachaInstanceColliderSize.y,
		gachaInstanceColliderSize.z,
		gachaInstanceColliderOffset.x,
		gachaInstanceColliderOffset.y,
		gachaInstanceColliderOffset.z
	);
}

void Game::SyncScenePropsFromPhysics()
{
	for (SceneProp& prop : sceneProps)
	{
		if (!prop.syncFromPhysics || prop.bodyId.IsInvalid())
			continue;

		if (prop.editLockPhysics)
			continue;

		ReadScenePropTransformFromBody(prop);

		if (prop.importedFromGlbScene && prop.model != nullptr)
		{
			SyncImportedEditorOffsetFromRuntime(prop);
		}
		else if (prop.parentIndex == -1)
		{
			SyncScenePropLocalFromWorld(prop);
		}
	}
}
bool Game::RaycastAgainstOBB(Vector3 rayOrigin, Vector3 rayDir,
	Vector3 boxCenter, Quaternion boxRotation,
	Vector3 boxSize, float maxDistance,
	float& outDistance) const
{
	Quaternion invQ = QuaternionInvert(boxRotation);

	Vector3 localOrigin = Vector3RotateByQuaternion(Vector3Subtract(rayOrigin, boxCenter), invQ);
	Vector3 localDir = Vector3RotateByQuaternion(rayDir, invQ);

	Vector3 half = {
		boxSize.x * 0.5f,
		boxSize.y * 0.5f,
		boxSize.z * 0.5f
	};

	float tMin = 0.0f;
	float tMax = maxDistance;

	auto slab = [&](float origin, float dir, float minB, float maxB) -> bool
		{
			if (fabsf(dir) < 0.00001f)
				return origin >= minB && origin <= maxB;

			float invD = 1.0f / dir;
			float t1 = (minB - origin) * invD;
			float t2 = (maxB - origin) * invD;

			if (t1 > t2) std::swap(t1, t2);

			tMin = (t1 > tMin) ? t1 : tMin;
			tMax = (t2 < tMax) ? t2 : tMax;

			return tMin <= tMax;
		};

	if (!slab(localOrigin.x, localDir.x, -half.x, half.x)) return false;
	if (!slab(localOrigin.y, localDir.y, -half.y, half.y)) return false;
	if (!slab(localOrigin.z, localDir.z, -half.z, half.z)) return false;

	outDistance = tMin;
	return tMin >= 0.0f && tMin <= maxDistance;
}

int Game::FindScenePropIndexByBody(JPH::BodyID id) const
{
	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		if (sceneProps[i].bodyId == id)
			return i;
	}

	return -1;
}

void Game::MarkInteractableScenePropCacheDirty()
{
	interactableScenePropCacheDirty = true;
}

void Game::RebuildInteractableScenePropIndexCache() const
{
	interactableScenePropIndices.clear();
	interactableScenePropIndices.reserve(sceneProps.size());

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		const SceneProp& prop = sceneProps[i];

		if (!prop.visible)
			continue;

		if (prop.model == nullptr)
			continue;

		bool isStoreControl =
			prop.itemTag == "store_open" ||
			prop.itemTag == "store_close" ||
			IsStoreControlProp(prop);

		if (!prop.canPickup && !isStoreControl)
			continue;

		interactableScenePropIndices.push_back(i);
	}

	interactableScenePropCacheSceneCount = (int)sceneProps.size();
	interactableScenePropCacheDirty = false;

	TraceLog(
		LOG_INFO,
		"Interactable cache rebuilt: %i / %i scene props",
		(int)interactableScenePropIndices.size(),
		(int)sceneProps.size()
	);
}

Game::InteractHit Game::FindInteractableBodyRaycast() const
{
	InteractHit best{};

	if (!physics)
		return best;

	if (interactableScenePropCacheDirty ||
		interactableScenePropCacheSceneCount != (int)sceneProps.size())
	{
		RebuildInteractableScenePropIndexCache();
	}

	Vector3 rayOrigin = camera.position;
	Vector3 rayDir = NormalizeSafe(GetCameraForward());

	auto IsNearInteractRay = [&](Vector3 center, float radius) -> bool
		{
			Vector3 toCenter = Vector3Subtract(center, rayOrigin);

			float forwardDist = Vector3DotProduct(toCenter, rayDir);

			if (forwardDist < -radius)
				return false;

			if (forwardDist > interactDistance + radius)
				return false;

			float closestT = Clamp(forwardDist, 0.0f, interactDistance);

			Vector3 closestPoint = Vector3Add(
				rayOrigin,
				Vector3Scale(rayDir, closestT)
			);

			Vector3 diff = Vector3Subtract(center, closestPoint);

			float distSq =
				diff.x * diff.x +
				diff.y * diff.y +
				diff.z * diff.z;

			return distSq <= radius * radius;
		};

	for (int cachedIndex = 0; cachedIndex < (int)interactableScenePropIndices.size(); cachedIndex++)
	{
		int i = interactableScenePropIndices[cachedIndex];

		if (i < 0 || i >= (int)sceneProps.size())
			continue;

		const SceneProp& prop = sceneProps[i];

		if (!prop.visible)
			continue;

		if (!prop.hasCollision)
			continue;

		if (!prop.useJoltCollider)
			continue;

		if (prop.bodyId.IsInvalid())
			continue;

		Quaternion rot = QuaternionFromEuler(
			prop.rotationDeg.x * DEG2RAD,
			prop.rotationDeg.y * DEG2RAD,
			prop.rotationDeg.z * DEG2RAD
		);

		Vector3 scaledOffset = GetScaledColliderOffset(prop);
		Vector3 rotatedOffset = Vector3RotateByQuaternion(scaledOffset, rot);

		Vector3 center = Vector3Add(prop.position, rotatedOffset);

		Vector3 size = {
			fabsf(prop.colliderSize.x * prop.scale.x) + 0.25f,
			fabsf(prop.colliderSize.y * prop.scale.y) + 0.25f,
			fabsf(prop.colliderSize.z * prop.scale.z) + 0.25f
		};

		float radius = 0.5f * sqrtf(
			size.x * size.x +
			size.y * size.y +
			size.z * size.z
		);

		radius += 0.25f;

		if (!IsNearInteractRay(center, radius))
			continue;

		float hitDistance = 0.0f;

		if (RaycastAgainstOBB(
			rayOrigin,
			rayDir,
			center,
			rot,
			size,
			interactDistance,
			hitDistance
		))
		{
			if (!best.valid || hitDistance < best.distance)
			{
				best.valid = true;
				best.bodyId = prop.bodyId;
				best.distance = hitDistance;
				best.scenePropIndex = i;
			}
		}
	}

	if (hasTestDynamicBox && !testDynamicBox.IsInvalid())
	{
		Vector3 center = physics->GetBodyPosition(testDynamicBox);
		Quaternion rot = physics->GetBodyRotation(testDynamicBox);

		float hitDistance = 0.0f;

		if (RaycastAgainstOBB(
			rayOrigin,
			rayDir,
			center,
			rot,
			{ 0.5f, 0.5f, 0.5f },
			interactDistance,
			hitDistance
		))
		{
			if (!best.valid || hitDistance < best.distance)
			{
				best.valid = true;
				best.bodyId = testDynamicBox;
				best.distance = hitDistance;
				best.scenePropIndex = -1;
			}
		}
	}

	return best;
}

void Game::PreparePickupScenePropsForPhysics()
{
	for (SceneProp& prop : sceneProps)
	{
		if (!prop.canPickup)
			continue;

		if (prop.model == nullptr)
			continue;

		BuildOutlineModelCache(prop.model);

		prop.hasCollision = true;
		prop.blocksPlayer = true;
		prop.useJoltCollider = true;
		prop.useNormalCollision = false;

		// Critical:
		// Pickup objects must be created as MOVING/Dynamic bodies,
		// then locked as kinematic when not held.
		prop.simulatePhysics = true;

		if (!hasHeldBody || heldScenePropIndex < 0 || &prop != &sceneProps[heldScenePropIndex])
		{
			prop.syncFromPhysics = false;
			prop.editLockPhysics = true;
		}

		if (prop.colliderSize.x <= 0.0f ||
			prop.colliderSize.y <= 0.0f ||
			prop.colliderSize.z <= 0.0f)
		{
			GetModelLocalBoundsCollider(
				prop.model,
				prop.colliderSize,
				prop.colliderOffset
			);
		}
	}
}

void Game::UpdateDialogueTextAnimation()
{
	if (dialogueTextComplete) return;

	double elapsed = GetTime() - dialogueTextStartTime;
	int visibleCharCount = (int)(elapsed * dialogueCharsPerSecond);

	size_t byteOffset = GetUtf8ByteOffsetForCharCount(fullDialogueText, visibleCharCount);

	if (byteOffset >= fullDialogueText.size())
	{
		CompleteDialogueTextAnimation();
		return;
	}

	SetDialogueLabelText(fullDialogueText.substr(0, byteOffset));
}

void Game::UpdateHeldBody()
{
	if (!hasHeldBody) return;

	Vector3 camForward = NormalizeSafe(GetCameraForward());
	Vector3 worldUp = { 0.0f, 1.0f, 0.0f };

	Vector3 flatForward = NormalizeSafe({ camForward.x, 0.0f, camForward.z });
	if (Vector3Length(flatForward) < 0.0001f)
		flatForward = { 0.0f, 0.0f, 1.0f };

	float camYawDeg = atan2f(flatForward.x, flatForward.z) * RAD2DEG;
	float camPitchDeg = asinf(Clamp(camForward.y, -1.0f, 1.0f)) * RAD2DEG;

	Vector3 holdOffsetLocal = { 0.0f, -0.2f, 0.0f };
	Vector3 holdRotationOffsetDeg = {
		heldPitchOffsetDeg,
		heldYawOffsetDeg,
		heldRollOffsetDeg
	};
	bool followCameraPitch = false;

	if (heldScenePropIndex >= 0 && heldScenePropIndex < (int)sceneProps.size())
	{
		const SceneProp& prop = sceneProps[heldScenePropIndex];

		holdOffsetLocal = prop.holdOffsetLocal;
		holdRotationOffsetDeg = prop.holdRotationOffsetDeg;
		followCameraPitch = prop.holdFollowCameraPitch;
	}

	Vector3 camRight = NormalizeSafe(Vector3CrossProduct(camForward, worldUp));
	if (Vector3Length(camRight) < 0.0001f)
		camRight = { 1.0f, 0.0f, 0.0f };

	Vector3 camUp = NormalizeSafe(Vector3CrossProduct(camRight, camForward));

	Quaternion holdQ{};

	if (inspectMode)
	{
		holdOffsetLocal = inspectHoldOffsetLocal;
		followCameraPitch = false;

		Quaternion qBaseYaw = QuaternionFromAxisAngle(worldUp, camYawDeg * DEG2RAD);

		Quaternion qModelOffset = QuaternionFromEuler(
			holdRotationOffsetDeg.x * DEG2RAD,
			holdRotationOffsetDeg.y * DEG2RAD,
			holdRotationOffsetDeg.z * DEG2RAD
		);

		Quaternion qInspectYaw = QuaternionFromAxisAngle(worldUp, inspectYawDeg * DEG2RAD);
		Quaternion qInspectPitch = QuaternionFromAxisAngle(camRight, inspectPitchDeg * DEG2RAD);

		holdQ = QuaternionMultiply(
			QuaternionMultiply(
				QuaternionMultiply(qInspectPitch, qInspectYaw),
				qBaseYaw
			),
			qModelOffset
		);
	}
	else
	{
		inspectSmoothingInitialized = false;

		Quaternion qYaw = QuaternionFromAxisAngle(worldUp, camYawDeg * DEG2RAD);
		Quaternion qBase = qYaw;

		if (followCameraPitch)
		{
			Vector3 rightAxis = NormalizeSafe(Vector3CrossProduct(flatForward, worldUp));
			if (Vector3Length(rightAxis) < 0.0001f)
				rightAxis = { 1.0f, 0.0f, 0.0f };

			Quaternion qPitch = QuaternionFromAxisAngle(rightAxis, camPitchDeg * DEG2RAD);
			qBase = QuaternionMultiply(qPitch, qYaw);
		}

		Quaternion qOffset = QuaternionFromEuler(
			holdRotationOffsetDeg.x * DEG2RAD,
			holdRotationOffsetDeg.y * DEG2RAD,
			holdRotationOffsetDeg.z * DEG2RAD
		);

		holdQ = QuaternionMultiply(qBase, qOffset);
	}

	float distanceFromCamera = holdDistance + holdOffsetLocal.z;
	if (inspectMode)
		distanceFromCamera = inspectDistance;

	Vector3 targetHoldPos = camera.position;
	targetHoldPos = Vector3Add(targetHoldPos, Vector3Scale(camForward, distanceFromCamera));
	targetHoldPos = Vector3Add(targetHoldPos, Vector3Scale(camRight, holdOffsetLocal.x));
	targetHoldPos = Vector3Add(targetHoldPos, Vector3Scale(camUp, holdOffsetLocal.y));

	Quaternion targetHoldRot = QuaternionNormalize(holdQ);

	if (inspectMode)
	{
		float dt = GetFrameTime();

		float posT = 1.0f - expf(-inspectPosFollowSharpness * dt);
		float rotT = 1.0f - expf(-inspectRotFollowSharpness * dt);

		if (!inspectSmoothingInitialized)
		{
			inspectSmoothedPos = targetHoldPos;
			inspectSmoothedRot = targetHoldRot;
			inspectSmoothingInitialized = true;
		}

		inspectSmoothedPos = Vector3Lerp(inspectSmoothedPos, targetHoldPos, posT);
		inspectSmoothedRot = QuaternionNormalize(
			QuaternionSlerp(inspectSmoothedRot, targetHoldRot, rotT)
		);

		physics->SetBodyPosition(heldBody, inspectSmoothedPos);
		physics->SetBodyRotation(heldBody, inspectSmoothedRot);
	}
	else
	{
		physics->SetBodyPosition(heldBody, targetHoldPos);
		physics->SetBodyRotation(heldBody, targetHoldRot);
	}

	physics->SetBodyLinearVelocity(heldBody, { 0.0f, 0.0f, 0.0f });
	physics->SetBodyAngularVelocity(heldBody, { 0.0f, 0.0f, 0.0f });

	if (heldScenePropIndex >= 0 && heldScenePropIndex < (int)sceneProps.size())
	{
		SceneProp& prop = sceneProps[heldScenePropIndex];

		ReadScenePropTransformFromBody(prop);

		if (prop.importedFromGlbScene && prop.model != nullptr)
		{
			SyncImportedEditorOffsetFromRuntime(prop);
		}
		else if (prop.parentIndex == -1)
		{
			SyncScenePropLocalFromWorld(prop);
		}
	}
}

void Game::StartHoldingBody(JPH::BodyID id)
{
	if (id.IsInvalid())
		return;

	if (!physics)
		return;

	int propIndex = FindScenePropIndexByBody(id);

	// Scene prop pickup.
	if (propIndex >= 0 && propIndex < (int)sceneProps.size())
	{
		SceneProp& prop = sceneProps[propIndex];

		if (!prop.canPickup)
		{
			TraceLog(
				LOG_WARNING,
				"StartHoldingBody rejected non-pickup prop: %s",
				prop.name.c_str()
			);
			return;
		}

		if (!prop.simulatePhysics)
		{
			TraceLog(
				LOG_WARNING,
				"StartHoldingBody rejected prop '%s' because simulatePhysics=false. Rebuild physics after PreparePickupScenePropsForPhysics().",
				prop.name.c_str()
			);
			return;
		}

		if (prop.bodyId.IsInvalid())
		{
			TraceLog(
				LOG_WARNING,
				"StartHoldingBody rejected prop '%s' because bodyId is invalid.",
				prop.name.c_str()
			);
			return;
		}
	}
	// Non-scene prop pickup: only allow the test dynamic box.
	else if (id != testDynamicBox)
	{
		TraceLog(LOG_WARNING, "StartHoldingBody rejected unknown BodyID.");
		return;
	}

	ClearHeldPropTarget();
	PushUndoSnapshot();

	heldBody = id;
	hasHeldBody = true;
	isHoldingBox = true;
	heldScenePropIndex = propIndex;
	ClearScenePropPlacementSpot(propIndex);

	physics->SetBodyMotionType(heldBody, JPH::EMotionType::Kinematic);
	physics->SetBodyLinearVelocity(heldBody, { 0.0f, 0.0f, 0.0f });
	physics->SetBodyAngularVelocity(heldBody, { 0.0f, 0.0f, 0.0f });
	physics->SetBodyIsSensor(heldBody, true);
	physics->SetBodyGravityFactor(heldBody, 0.0f);
}

int Game::ResolveCustomerOwnerForCounterProp(int propIndex) const
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return -1;

	const SceneProp& prop = sceneProps[propIndex];

	// Only runtime customer-created items should be used for buyer scan return.
	// Shelf items / normal props / seller items should not complete buyer flow.
	if (!prop.placedByCustomer)
		return -1;

	// Direct owner must be a browser and must point to this exact prop.
	if (prop.owningCustomerIndex >= 0 &&
		prop.owningCustomerIndex < (int)customers.size())
	{
		const Customer& customer = customers[prop.owningCustomerIndex];

		if (customer.role == CustomerRole::Browser &&
			customer.counterItemScenePropIndex == propIndex)
		{
			return prop.owningCustomerIndex;
		}

		return -1;
	}

	// Repair missing owner only if this exact prop is already stored
	// as a browser customer's counter item.
	for (int i = 0; i < (int)customers.size(); i++)
	{
		const Customer& customer = customers[i];

		if (customer.role != CustomerRole::Browser)
			continue;

		if (customer.counterItemScenePropIndex == propIndex)
			return i;
	}

	return -1;
}

void Game::ClearHeldStateAfterCustomerTakesItem(int propIndex)
{
	if (!hasHeldBody)
		return;

	if (heldScenePropIndex != propIndex)
		return;

	ClearHeldPropTarget();

	hasHeldBody = false;
	isHoldingBox = false;
	heldScenePropIndex = -1;
	heldBody = JPH::BodyID();

	placementPreviewValid = false;
	placementPreviewPropIndex = -1;
	targetedPlacementSpotIndex = -1;
}

void Game::StopHoldingBody()
{
	if (!hasHeldBody) return;

	if (inspectMode)
	{
		ExitInspectMode();
	}
	int releasedIndex = heldScenePropIndex;

	if (releasedIndex >= 0 && releasedIndex < (int)sceneProps.size())
	{
		SceneProp& prop = sceneProps[releasedIndex];

		prop.simulatePhysics = true;
		prop.syncFromPhysics = true;
		prop.editLockPhysics = false;

		ReadScenePropTransformFromBody(prop);

		if (prop.importedFromGlbScene && prop.model != nullptr)
		{
			SyncImportedEditorOffsetFromRuntime(prop);
		}

		if (TrySnapHeldPropToPlacementSpot(releasedIndex))
		{
			hasHeldBody = false;
			isHoldingBox = false;
			heldScenePropIndex = -1;
			heldBody = JPH::BodyID();

			ClearHeldPropTarget();

			customerNavGridDirty = true;
			MarkShadowMapsDirty();

			return;
		}

		Vector3 camForward = NormalizeSafe(GetCameraForward());

		const float maxPlaceDistance = 1.0f;
		const float supportProbeHeight = 2.0f;
		const float fallbackDistance = 1.25f;

		PlacementHit frontHit = FindPlacementSurfaceRaycast(
			camera.position,
			camForward,
			maxPlaceDistance,
			releasedIndex
		);

		Vector3 placementAnchor;
		if (frontHit.valid)
		{
			placementAnchor = frontHit.point;
		}
		else
		{
			// use full camera ray at max range
			placementAnchor = Vector3Add(
				camera.position,
				Vector3Scale(camForward, maxPlaceDistance)
			);
		}

		PlacementHit supportHit = FindDownwardPlacementRaycast(
			placementAnchor,
			supportProbeHeight,
			releasedIndex
		);

		float dropYawDeg = prop.rotationDeg.y;

		if (prop.snapUprightOnDrop)
		{
			Vector3 toPlayer = {
				player.m_pos.x - prop.position.x,
				0.0f,
				player.m_pos.z - prop.position.z
			};

			if (Vector3Length(toPlayer) > 0.0001f)
			{
				toPlayer = NormalizeSafe(toPlayer);
				dropYawDeg = atan2f(toPlayer.x, toPlayer.z) * RAD2DEG;
			}

			prop.rotationDeg.x = 0.0f;
			prop.rotationDeg.y = dropYawDeg + prop.dropRotationOffsetDeg.y;
			prop.rotationDeg.z = 0.0f;
		}
		else
		{
			prop.rotationDeg.x += prop.dropRotationOffsetDeg.x;
			prop.rotationDeg.y += prop.dropRotationOffsetDeg.y;
			prop.rotationDeg.z += prop.dropRotationOffsetDeg.z;
		}

		Quaternion finalQ = QuaternionFromEuler(
			prop.rotationDeg.x * DEG2RAD,
			prop.rotationDeg.y * DEG2RAD,
			prop.rotationDeg.z * DEG2RAD
		);

		Vector3 scaledOffset = GetScaledColliderOffset(prop);
		Vector3 rotatedOffset = Vector3RotateByQuaternion(scaledOffset, finalQ);
		float halfHeight = (prop.colliderSize.y * prop.scale.y) * 0.5f;

		if (supportHit.valid)
		{
			Vector3 center = supportHit.point;
			center.y += halfHeight;
			prop.position = Vector3Subtract(center, rotatedOffset);
		}
		else
		{
			Vector3 fallbackPos = Vector3Add(
				camera.position,
				Vector3Scale(camForward, fallbackDistance)
			);
			prop.position = Vector3Subtract(fallbackPos, rotatedOffset);
		}

		ApplyScenePropTransformToBody(prop);

		if (prop.importedFromGlbScene && prop.model != nullptr)
		{
			SyncImportedEditorOffsetFromRuntime(prop);
		}
		else if (prop.parentIndex == -1)
		{
			SyncScenePropLocalFromWorld(prop);
		}
	}

	physics->SetBodyMotionType(heldBody, JPH::EMotionType::Dynamic);
	physics->SetBodyLinearVelocity(heldBody, { 0.0f, 0.0f, 0.0f });
	physics->SetBodyAngularVelocity(heldBody, { 0.0f, 0.0f, 0.0f });
	physics->SetBodyIsSensor(heldBody, false);
	physics->SetBodyGravityFactor(heldBody, 1.0f);

	hasHeldBody = false;
	isHoldingBox = false;
	heldScenePropIndex = -1;
	heldBody = JPH::BodyID();
}

void Game::DisableShadowsForShader(Shader shader) const
{
	int zero = 0;

	SetShaderValue(
		shader,
		GetShaderLocation(shader, "shadowCasterCount"),
		&zero,
		SHADER_UNIFORM_INT
	);
}

bool Game::IsHeldInspectItemTag(const std::string& tag) const
{
	if (!hasHeldBody)
		return false;

	if (heldScenePropIndex < 0 ||
		heldScenePropIndex >= (int)sceneProps.size())
	{
		return false;
	}

	const SceneProp& prop = sceneProps[heldScenePropIndex];

	std::string inspectTag = prop.inspectDialogueTag;

	if (inspectTag.empty())
		inspectTag = prop.itemTag;

	inspectTag = SanitizeVoiceToken(inspectTag);

	return inspectTag == tag;
}

void Game::PauseRecordPlayerForMusicBoxInspect()
{
	musicBoxPausedRecordPlayer = false;
	musicBoxSavedRecordPlayerPaused = recordPlayerPaused;

	if (!recordPlayerMusicLoaded)
		return;

	if (!IsMusicStreamPlaying(recordPlayerMusic))
		return;

	PauseMusicStream(recordPlayerMusic);

	// Mark it paused so your normal record-player update does not treat it
	// as actively playing during the music-box inspect moment.
	recordPlayerPaused = true;

	musicBoxPausedRecordPlayer = true;
}
void Game::ResumeRecordPlayerAfterMusicBoxInspect()
{
	if (!musicBoxPausedRecordPlayer)
		return;

	musicBoxPausedRecordPlayer = false;

	if (!recordPlayerMusicLoaded)
	{
		recordPlayerPaused = musicBoxSavedRecordPlayerPaused;
		return;
	}

	// Only resume if it was NOT already paused before the music-box effect.
	if (!musicBoxSavedRecordPlayerPaused)
	{
		ResumeMusicStream(recordPlayerMusic);
		recordPlayerPaused = false;
	}
	else
	{
		recordPlayerPaused = true;
	}

	musicBoxSavedRecordPlayerPaused = false;
}
float Game::GetMusicBoxSceneBlackoutAmount() const
{
	return Clamp(musicBoxSceneBlackoutAmount, 0.0f, 1.0f);
}

void Game::BeginMusicBoxInspectEasterEgg()
{
	if (!IsHeldInspectItemTag("music_box"))
		return;

	if (musicBoxInspectActive)
		return;

	musicBoxInspectActive = true;
	musicBoxInspectTimer = 0.0f;

	musicBoxInspectLightMultiplier = 1.0f;
	musicBoxInspectAmbientMultiplier = 0.15f;

	PauseRecordPlayerForMusicBoxInspect();
}

void Game::EndMusicBoxInspectEasterEgg()
{
	bool wasActive = musicBoxInspectActive;

	musicBoxInspectActive = false;
	musicBoxInspectTimer = 0.0f;

	musicBoxInspectLightMultiplier = 1.0f;
	musicBoxInspectAmbientMultiplier = 1.0f;

	// Do not reset musicBoxSceneBlackoutAmount here.
	// Let it fade out naturally.

	if (wasActive || musicBoxPausedRecordPlayer)
	{
		ResumeRecordPlayerAfterMusicBoxInspect();
	}
}

float Game::GetMusicBoxInspectFlicker01() const
{
	if (!musicBoxInspectActive)
		return 1.0f;

	float t = musicBoxInspectTimer;

	// Layered flicker: slow pulse + fast unstable flashes.
	float slow = 0.5f + 0.5f * sinf(t * 5.0f);
	float fast = 0.5f + 0.5f * sinf(t * 37.0f + sinf(t * 11.0f) * 3.0f);

	float flicker = slow * 0.35f + fast * 0.65f;

	// Occasional near-blackouts.
	float blackoutPulse = sinf(t * 13.0f) * sinf(t * 29.0f);

	if (blackoutPulse > 0.82f)
	{
		flicker *= 0.12f;
	}
	else if (blackoutPulse > 0.65f)
	{
		flicker *= 0.35f;
	}

	return Clamp(flicker, 0.05f, 1.0f);
}
void Game::UpdateMusicBoxInspectEasterEgg(float dt)
{
	bool wantsBlackout =
		inspectMode &&
		musicBoxInspectActive &&
		musicBoxInspectBlackoutScene;

	float targetBlackout = wantsBlackout ? 1.0f : 0.0f;

	float fadeSeconds = wantsBlackout
		? musicBoxSceneBlackoutFadeInSeconds
		: musicBoxSceneBlackoutFadeOutSeconds;

	fadeSeconds = fmaxf(fadeSeconds, 0.01f);

	float fadeStep = dt / fadeSeconds;

	musicBoxSceneBlackoutAmount = MoveTowardsFloatLocal(
		musicBoxSceneBlackoutAmount,
		targetBlackout,
		fadeStep
	);

	if (!musicBoxInspectActive)
	{
		musicBoxInspectLightMultiplier = MoveTowardsFloatLocal(
			musicBoxInspectLightMultiplier,
			1.0f,
			dt * 6.0f
		);

		musicBoxInspectAmbientMultiplier = MoveTowardsFloatLocal(
			musicBoxInspectAmbientMultiplier,
			1.0f,
			dt * 6.0f
		);

		return;
	}

	if (!inspectMode || !hasHeldBody || !IsHeldInspectItemTag("music_box"))
	{
		EndMusicBoxInspectEasterEgg();
		return;
	}

	musicBoxInspectTimer += dt;

	float flicker = GetMusicBoxInspectFlicker01();

	musicBoxInspectLightMultiplier = Lerp(
		musicBoxInspectLightMultiplier,
		flicker,
		1.0f - expf(-18.0f * dt)
	);

	musicBoxInspectAmbientMultiplier = Lerp(
		musicBoxInspectAmbientMultiplier,
		0.06f + flicker * 0.16f,
		1.0f - expf(-10.0f * dt)
	);
}

void Game::RecreatePostProcessTargets()
{
	EnsureRenderTargetsMatchWindow();
}
void Game::DrawWorldToTarget(const Camera3D& cam)
{
	double worldStart = GetTime();
	double t = GetTime();

	BeginTextureMode(worldTarget);
	ClearBackground(WHITE);
	BeginMode3D(cam);

	perf.worldTargetBeginMs = MsSince(t);

	t = GetTime();
	DrawSkybox();
	perf.drawSkyboxMs = MsSince(t);

	t = GetTime();
	player.draw();
	perf.drawPlayerMs = MsSince(t);

	t = GetTime();

	if (ShouldMusicBoxBlackoutScene())
	{
		UploadBlackoutLightingToShader(
			pbrShader,
			pbrLightLocs,
			cam.position
		);

		UploadBlackoutLightingToShader(
			animatedPbrShader,
			animatedLightLocs,
			cam.position
		);

		UploadBlackoutLightingToShader(
			instancedPbrShader,
			instancedLightLocs,
			cam.position
		);

		// Optional but recommended:
		// shadow uniforms are not useful when scene lights are off.
		DisableShadowsForShader(pbrShader);
		DisableShadowsForShader(animatedPbrShader);
		DisableShadowsForShader(instancedPbrShader);
	}
	else
	{
		UploadLightsToShader(pbrShader);
		UploadLightsToShader(animatedPbrShader);
		UploadLightsToShader(instancedPbrShader);

		UploadShadowUniformsToShader(pbrShader);
		UploadShadowUniformsToShader(animatedPbrShader);
		UploadShadowUniformsToShader(instancedPbrShader);
	}

	perf.shadowUniformUploadMs = MsSince(t);

	t = GetTime();

	AttachShadowTextureToSceneMaterials();

	perf.shadowTextureAttachMs = MsSince(t);

	perf.attachShadowTextureMs =
		perf.shadowUniformUploadMs +
		perf.shadowTextureAttachMs;

	t = GetTime();
	DrawLevel();
	perf.drawLevelMs = MsSince(t);

	if (instancedPropsDirty)
	{
		RebuildInstancedPropTransforms();
	}

	t = GetTime();
	DrawGachaBallInstances(cam);
	perf.drawGachaBallsMs = MsSince(t);

	t = GetTime();
	DrawGachaMachineInstanceTest(cam);
	perf.drawGachaMachinesMs = MsSince(t);

	perf.drawGachaInstancedMs =
		perf.drawGachaBallsMs +
		perf.drawGachaMachinesMs;

	t = GetTime();
	DrawBasketCartridgeInstances(cam);
	DrawBasketInstances(cam);
	perf.drawBasketInstancesMs = MsSince(t);

	t = GetTime();
	DrawCustomers();
	perf.drawCustomersMs = MsSince(t);

	t = GetTime();

	DrawSceneProps(cam);
	perf.drawScenePropsMs = MsSince(t);

	t = GetTime();
	//DrawStoreBlockout();
	perf.drawBlockoutMs = MsSince(t);

	DrawHeldPlacementPreview();

	t = GetTime();

	if (editMode && drawEditorPointDebug)
	{
		DrawCustomerPOIDebug();
		DrawCustomerAIDebug();
		DrawItemPlacementSpotDebug();
		DrawCinematicTriggerDebug();
	}

	if (editMode &&
		drawSelectedScenePropBounds &&
		selectedScenePropIndex >= 0 &&
		selectedScenePropIndex < (int)sceneProps.size())
	{
		BoundingBox bounds{};

		if (GetScenePropRenderBoundsWorld(sceneProps[selectedScenePropIndex], bounds))
		{
			DrawBoundingBox(bounds, ORANGE);
		}
	}

	perf.drawEditorDebugMs = MsSince(t);

	t = GetTime();
	DrawInteractableOutline(cam);
	perf.drawOutlineMs = MsSince(t);

	rlEnableBackfaceCulling();

	t = GetTime();

	if (drawPhysicsDebugBounds)
	{
		DrawPhysicsDebug();
	}

	perf.drawPhysicsDebugMs = MsSince(t);

	if (drawTestDynamicBoxDebug && hasTestDynamicBox)
	{
		Vector3 p = physics->GetBodyPosition(testDynamicBox);
		DrawCubeV(p, { 0.5f, 0.5f, 0.5f }, RED);
		DrawCubeWiresV(p, { 0.5f, 0.5f, 0.5f }, MAROON);
	}

	t = GetTime();
	EndMode3D();
	EndTextureMode();
	perf.worldTargetEndMs = MsSince(t);

	float worldTotal = MsSince(worldStart);

	perf.worldKnownPartsMs =
		perf.worldTargetBeginMs +
		perf.drawSkyboxMs +
		perf.drawPlayerMs +
		perf.attachShadowTextureMs +
		perf.drawLevelMs +
		perf.drawGachaInstancedMs +
		perf.drawBasketInstancesMs +
		perf.drawCustomersMs +
		perf.drawScenePropsMs +
		perf.drawBlockoutMs +
		perf.drawEditorDebugMs +
		perf.drawOutlineMs +
		perf.drawPhysicsDebugMs +
		perf.worldTargetEndMs;

	perf.worldUnaccountedMs = worldTotal - perf.worldKnownPartsMs;
}
int Game::CountCustomersUsingPOI(int poiIndex, int ignoreCustomerIndex) const
{
	int count = 0;

	for (int i = 0; i < (int)customers.size(); i++)
	{
		if (i == ignoreCustomerIndex)
			continue;

		const Customer& customer = customers[i];

		if (customer.pendingDespawn)
			continue;

		if (customer.destinationPOIIndex == poiIndex ||
			customer.targetPOIIndex == poiIndex ||
			customer.currentPOIIndex == poiIndex ||
			customer.assignedItemPOIIndex == poiIndex ||
			customer.assignedQueueSlotIndex == poiIndex)
		{
			count++;
		}
	}

	return count;
}
bool Game::IsPOIAvailableForCustomer(int customerIndex, int poiIndex) const
{
	if (poiIndex < 0 || poiIndex >= (int)customerPOIs.size())
		return false;

	const CustomerPOI& poi = customerPOIs[poiIndex];

	if (!poi.enabled)
		return false;

	if (!poi.exclusive)
		return true;

	int usedCount = CountCustomersUsingPOI(poiIndex, customerIndex);

	return usedCount < poi.capacity;
}
float Game::GetRandomPOIWaitSeconds(const CustomerPOI& poi) const
{
	float minWait = fminf(poi.waitSecondsMin, poi.waitSecondsMax);
	float maxWait = fmaxf(poi.waitSecondsMin, poi.waitSecondsMax);

	int value = GetRandomValue(
		(int)(minWait * 100.0f),
		(int)(maxWait * 100.0f)
	);

	return (float)value / 100.0f;
}
void Game::DrawCustomerPOIDebug() const
{
	for (int i = 0; i < (int)customerPOIs.size(); i++)
	{
		const CustomerPOI& poi = customerPOIs[i];

		Color color = poi.enabled ? SKYBLUE : DARKGRAY;

		if (!poi.stopPoint)
			color = PURPLE;

		if (i == selectedCustomerPOI)
			color = YELLOW;

		DrawSphere(poi.position, 0.08f, color);

		DrawCircle3D(
			poi.position,
			poi.radius,
			Vector3{ 1.0f, 0.0f, 0.0f },
			90.0f,
			color
		);

		DrawLine3D(
			poi.position,
			Vector3Add(poi.position, Vector3{ 0.0f, 1.0f, 0.0f }),
			color
		);

		if (poi.useFacingDirection)
		{
			float yawRad = poi.facingYawDeg * DEG2RAD;

			Vector3 forward = {
				sinf(yawRad),
				0.0f,
				cosf(yawRad)
			};

			Vector3 start = Vector3Add(
				poi.position,
				Vector3{ 0.0f, 0.08f, 0.0f }
			);

			Vector3 end = Vector3Add(
				start,
				Vector3Scale(forward, 0.7f)
			);

			DrawLine3D(start, end, ORANGE);
			DrawSphere(end, 0.05f, ORANGE);
		}

	}
}

void Game::DrawItemPlacementSpotDebug() const
{
	for (int i = 0; i < (int)itemPlacementSpots.size(); i++)
	{
		const ItemPlacementSpot& spot = itemPlacementSpots[i];

		Color color = YELLOW;

		switch (spot.kind)
		{
		case ItemPlacementSpotKind::CounterOffer:
			color = ORANGE;
			break;

		case ItemPlacementSpotKind::CounterScan:
			color = SKYBLUE;
			break;

		case ItemPlacementSpotKind::ShelfSlot:
			color = GREEN;
			break;
		}

		if (!spot.enabled)
		{
			color = DARKGRAY;
		}

		if (i == targetedPlacementSpotIndex)
		{
			color = GOLD;
		}

		// Center marker
		DrawSphere(spot.position, 0.06f, color);

		// Snap radius circle on horizontal plane
		DrawCircle3D(
			spot.position,
			spot.snapRadius,
			Vector3{ 1.0f, 0.0f, 0.0f },
			90.0f,
			color
		);

		// Up line
		DrawLine3D(
			spot.position,
			Vector3Add(spot.position, Vector3{ 0.0f, 0.35f, 0.0f }),
			color
		);

		// Forward direction line based on spot.rotationDeg.y
		float yawRad = spot.rotationDeg.y * DEG2RAD;

		Vector3 forward = {
			sinf(yawRad),
			0.0f,
			cosf(yawRad)
		};

		Vector3 forwardEnd = Vector3Add(
			spot.position,
			Vector3Scale(forward, 0.35f)
		);

		DrawLine3D(
			spot.position,
			forwardEnd,
			color
		);

		// Small placement box
		Vector3 boxSize = {
			0.18f * spot.scale.x,
			0.04f * spot.scale.y,
			0.18f * spot.scale.z
		};

		DrawCubeWiresV(
			spot.position,
			boxSize,
			color
		);
	}

}
void Game::UploadShadowUniformsToShader(Shader shader) const
{
	const ShadowUniformLocations* locs = nullptr;

	if (shader.id == pbrShader.id)
	{
		locs = &pbrShadowLocs;
	}
	else if (shader.id == animatedPbrShader.id)
	{
		locs = &animatedShadowLocs;
	}
	else if (shader.id == instancedPbrShader.id)
	{
		locs = &instancedShadowLocs;
	}

	if (locs == nullptr)
		return;

	int count = shadowCasters[0].active ? 1 : 0;

	SetShaderValueIfValid(
		shader,
		locs->shadowCasterCount,
		&count,
		SHADER_UNIFORM_INT
	);

	if (count <= 0)
		return;

	if (locs->shadowLightVP0 >= 0)
	{
		SetShaderValueMatrix(
			shader,
			locs->shadowLightVP0,
			shadowCasters[0].lightVP
		);
	}

	int lightIndex = shadowCasters[0].lightIndex;
	float bias = shadowCasters[0].bias;
	float strength = shadowCasters[0].strength;

	SetShaderValueIfValid(
		shader,
		locs->shadowLightIndex0,
		&lightIndex,
		SHADER_UNIFORM_INT
	);

	SetShaderValueIfValid(
		shader,
		locs->shadowBias0,
		&bias,
		SHADER_UNIFORM_FLOAT
	);

	SetShaderValueIfValid(
		shader,
		locs->shadowStrength0,
		&strength,
		SHADER_UNIFORM_FLOAT
	);

	if (locs->shadowMap0 >= 0 && shadowCasters[0].shadowMap.texture.id != 0)
	{
		rlEnableShader(shader.id);

		rlActiveTextureSlot(10);
		rlEnableTexture(shadowCasters[0].shadowMap.texture.id);
		rlSetUniformSampler(locs->shadowMap0, 10);

		rlActiveTextureSlot(0);

		rlDisableShader();
	}
}

void Game::DrawBlurredWorldToScreen()
{
	if (worldTarget.id == 0 || blurPingTarget.id == 0 || blurPongTarget.id == 0)
		return;

	RenderTexture2D* input = &worldTarget;
	RenderTexture2D* output = &blurPingTarget;

	for (int i = 0; i < inspectBlurPasses; i++)
	{
		// Horizontal pass
		{
			Rectangle src = {
				0.0f,
				0.0f,
				(float)input->texture.width,
				-(float)input->texture.height
			};

			Rectangle dst = {
				0.0f,
				0.0f,
				(float)output->texture.width,
				(float)output->texture.height
			};

			Vector2 texelSize = {
				1.0f / (float)input->texture.width,
				1.0f / (float)input->texture.height
			};

			BeginTextureMode(*output);
			ClearBackground(BLANK);

			BeginShaderMode(blurShader);

			Vector2 dir = { 1.0f, 0.0f };
			SetShaderValue(blurShader, blurDirectionLoc, &dir, SHADER_UNIFORM_VEC2);
			SetShaderValue(blurShader, blurTexelSizeLoc, &texelSize, SHADER_UNIFORM_VEC2);

			DrawTexturePro(input->texture, src, dst, { 0.0f, 0.0f }, 0.0f, WHITE);

			EndShaderMode();
			EndTextureMode();
		}

		input = &blurPingTarget;
		output = &blurPongTarget;

		// Vertical pass
		{
			Rectangle src = {
				0.0f,
				0.0f,
				(float)input->texture.width,
				-(float)input->texture.height
			};

			Rectangle dst = {
				0.0f,
				0.0f,
				(float)output->texture.width,
				(float)output->texture.height
			};

			Vector2 texelSize = {
				1.0f / (float)input->texture.width,
				1.0f / (float)input->texture.height
			};

			BeginTextureMode(*output);
			ClearBackground(BLANK);

			BeginShaderMode(blurShader);

			Vector2 dir = { 0.0f, 1.0f };
			SetShaderValue(blurShader, blurDirectionLoc, &dir, SHADER_UNIFORM_VEC2);
			SetShaderValue(blurShader, blurTexelSizeLoc, &texelSize, SHADER_UNIFORM_VEC2);

			DrawTexturePro(input->texture, src, dst, { 0.0f, 0.0f }, 0.0f, WHITE);

			EndShaderMode();
			EndTextureMode();
		}

		input = &blurPongTarget;
		output = &blurPingTarget;
	}

	Rectangle finalSrc = {
		0.0f,
		0.0f,
		(float)input->texture.width,
		-(float)input->texture.height
	};

	Rectangle finalDst = {
		0.0f,
		0.0f,
		(float)GetScreenWidth(),
		(float)GetScreenHeight()
	};

	DrawTexturePro(input->texture, finalSrc, finalDst, { 0.0f, 0.0f }, 0.0f, WHITE);
}

void Game::RecreateHeldPropTarget()
{
	EnsureRenderTargetsMatchWindow();
}

bool Game::ShouldMusicBoxBlackoutScene() const
{
	return inspectMode &&
		musicBoxInspectActive &&
		musicBoxInspectBlackoutScene;
}

void Game::UploadBlackoutLightingToShader(
	Shader shader,
	const LightUniformLocations& locs,
	Vector3 cameraPosition
) const
{
	if (shader.id == 0)
		return;

	int zeroLightCount = 0;

	float cameraPos[3] = {
		cameraPosition.x,
		cameraPosition.y,
		cameraPosition.z
	};

	Vector3 darkAmbientColor = {
		0.015f,
		0.015f,
		0.020f
	};

	float darkAmbient = 0.006f;

	SetShaderValueIfValid(
		shader,
		locs.numOfLights,
		&zeroLightCount,
		SHADER_UNIFORM_INT
	);

	SetShaderValueIfValid(
		shader,
		locs.viewPos,
		cameraPos,
		SHADER_UNIFORM_VEC3
	);

	SetShaderValueIfValid(
		shader,
		locs.ambientColor,
		&darkAmbientColor,
		SHADER_UNIFORM_VEC3
	);

	SetShaderValueIfValid(
		shader,
		locs.ambient,
		&darkAmbient,
		SHADER_UNIFORM_FLOAT
	);

	int disabled = 0;

	for (int i = 0; i < MAX_LIGHTS; i++)
	{
		SetShaderValueIfValid(
			shader,
			locs.enabled[i],
			&disabled,
			SHADER_UNIFORM_INT
		);
	}

}

void Game::DrawScenePropByIndexPass(int propIndex, bool transparentPass) const
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return;

	const SceneProp& prop = sceneProps[propIndex];

	if (!prop.visible || prop.model == nullptr)
		return;

	const Model& model = *prop.model;
	Matrix transform = GetScenePropDrawMatrix(prop);

	const Vector2 oneTiling = { 1.0f, 1.0f };
	const Vector2 zeroOffset = { 0.0f, 0.0f };

	bool forceOpaqueShadowReceiver = false;

	if (prop.importedFromGlbScene)
	{
		std::string shadowReceiverName =
			prop.name + " " + prop.sourceNodeName;

		std::transform(
			shadowReceiverName.begin(),
			shadowReceiverName.end(),
			shadowReceiverName.begin(),
			[](unsigned char c) { return (char)std::tolower(c); }
		);

		if (shadowReceiverName.find("ground") != std::string::npos ||
			shadowReceiverName.find("floor") != std::string::npos ||
			shadowReceiverName.find("road") != std::string::npos ||
			shadowReceiverName.find("asphalt") != std::string::npos ||
			shadowReceiverName.find("sidewalk") != std::string::npos ||
			shadowReceiverName.find("pavement") != std::string::npos)
		{
			forceOpaqueShadowReceiver = true;
		}
	}

	SetShaderValue(pbrShader, textureTilingLoc, &oneTiling, SHADER_UNIFORM_VEC2);
	SetShaderValue(pbrShader, offsetLoc, &zeroOffset, SHADER_UNIFORM_VEC2);

	for (int meshIndex = 0; meshIndex < model.meshCount; meshIndex++)
	{
		int matIndex = 0;

		if (model.meshMaterial != nullptr)
		{
			matIndex = model.meshMaterial[meshIndex];
		}

		if (matIndex < 0 || matIndex >= model.materialCount)
		{
			matIndex = 0;
		}

		Material mat = model.materials[matIndex];
		mat.shader = pbrShader;
		int alphaMode = 0;
		float alphaCutoff = 0.5f;
		float baseAlpha = 1.0f;

		// Prefer per-mesh primitive alpha.
		// This avoids wrong mesh transparency when material indices do not map cleanly.
		if (meshIndex >= 0 && meshIndex < (int)prop.meshAlphaModes.size())
		{
			alphaMode = prop.meshAlphaModes[meshIndex];
		}

		if (meshIndex >= 0 && meshIndex < (int)prop.meshAlphaCutoffs.size())
		{
			alphaCutoff = prop.meshAlphaCutoffs[meshIndex];
		}

		if (meshIndex >= 0 && meshIndex < (int)prop.meshBaseAlphas.size())
		{
			baseAlpha = prop.meshBaseAlphas[meshIndex];
		}
		else
		{
			// Fallback for older/manual props.
			if (matIndex >= 0 && matIndex < (int)prop.materialAlphaModes.size())
			{
				alphaMode = prop.materialAlphaModes[matIndex];
			}

			if (matIndex >= 0 && matIndex < (int)prop.materialAlphaCutoffs.size())
			{
				alphaCutoff = prop.materialAlphaCutoffs[matIndex];
			}
		}

		if (ContainsMaterialIndex(prop.transparentMaterialIndices, matIndex))
		{
			alphaMode = 2;
		}


		if (forceOpaqueShadowReceiver)
		{
			alphaMode = 0;
			alphaCutoff = 0.5f;
			baseAlpha = 1.0f;
		}

		const bool isHeldScenePropRender =
			hasHeldBody &&
			propIndex == heldScenePropIndex;

		// Held / inspected props are rendered into a separate transparent render target.
		// Alpha-blended character parts can create black patches there.
		// Use alpha mask instead: discard invisible pixels, draw visible pixels opaque.
		if (isHeldScenePropRender)
		{
			alphaMode = 1;          // masked / cutout
			alphaCutoff = 0.05f;    // low cutoff so mostly everything remains visible
			baseAlpha = 1.0f;
		}

		bool materialIsTransparent = alphaMode == 2;

		static bool loggedTransparentMaterial = false;

		if (materialIsTransparent && !loggedTransparentMaterial)
		{
			TraceLog(
				LOG_INFO,
				"Drawing transparent scene prop material: prop='%s' mesh=%i mat=%i alphaMode=%i",
				prop.name.c_str(),
				meshIndex,
				matIndex,
				alphaMode
			);

			loggedTransparentMaterial = true;
		}

		// Opaque pass draws opaque + masked.
		if (!transparentPass && materialIsTransparent)
			continue;

		// Transparent pass draws only blended materials.
		if (transparentPass && !materialIsTransparent)
			continue;

		bool hasAlbedoMap = mat.maps[MATERIAL_MAP_ALBEDO].texture.id > 1;
		bool hasNormalMap = mat.maps[MATERIAL_MAP_NORMAL].texture.id > 1;
		bool meshHasTangents = model.meshes[meshIndex].tangents != nullptr;

		bool hasMetallicMap = mat.maps[MATERIAL_MAP_METALNESS].texture.id > 1;
		bool hasRoughnessMap = mat.maps[MATERIAL_MAP_ROUGHNESS].texture.id > 1;
		bool hasAOMap = mat.maps[MATERIAL_MAP_OCCLUSION].texture.id > 1;
		bool hasEmissiveMap = mat.maps[MATERIAL_MAP_EMISSION].texture.id > 1;

		float metallic = hasMetallicMap ? mat.maps[MATERIAL_MAP_METALNESS].value : 0.0f;
		float roughness = hasRoughnessMap ? mat.maps[MATERIAL_MAP_ROUGHNESS].value : 0.65f;
		float ao = hasAOMap ? mat.maps[MATERIAL_MAP_OCCLUSION].value : 1.0f;

		// Texture factor safety.
		// Some imported maps can have value 0 even though the texture itself is valid.
		if (hasRoughnessMap && roughness <= 0.001f)
			roughness = 1.0f;

		if (hasAOMap && ao <= 0.001f)
			ao = 1.0f;

		float normalStrength = (hasNormalMap && meshHasTangents) ? 1.0f : 0.0f;
		float emissiveIntensity = hasEmissiveMap ? 1.0f : 0.0f;

		float reflectionStrengthValue = reflectionStrength;

		if (prop.importedFromGlbScene)
		{
			reflectionStrengthValue = 0.025f;

			if (meshIndex >= 0 && meshIndex < (int)prop.meshReflectionStrengths.size())
			{
				reflectionStrengthValue = prop.meshReflectionStrengths[meshIndex];
			}
			else if (matIndex >= 0 && matIndex < (int)prop.materialReflectionStrengths.size())
			{
				reflectionStrengthValue = prop.materialReflectionStrengths[matIndex];
			}
		}

		Vector4 albedoColor = ColorNormalize(mat.maps[MATERIAL_MAP_ALBEDO].color);

		if (prop.importedFromGlbScene && hasAlbedoMap)
		{
			// Prevent imported material factors from accidentally darkening the texture.
			// Keep alpha from the material, but force RGB tint to neutral white.
			albedoColor.x = 1.0f;
			albedoColor.y = 1.0f;
			albedoColor.z = 1.0f;
		}

		if (prop.importedFromGlbScene)
		{
			albedoColor.w = baseAlpha;
		}


		Vector4 emissiveColor = ColorNormalize(mat.maps[MATERIAL_MAP_EMISSION].color);

		if (isHeldScenePropRender)
		{
			albedoColor.w = 1.0f;
			mat.maps[MATERIAL_MAP_ALBEDO].color.a = 255;
		}



		SetShaderValue(pbrShader, metallicValueLoc, &metallic, SHADER_UNIFORM_FLOAT);
		SetShaderValue(pbrShader, roughnessValueLoc, &roughness, SHADER_UNIFORM_FLOAT);
		SetShaderValue(pbrShader, aoValueLoc, &ao, SHADER_UNIFORM_FLOAT);
		SetShaderValue(pbrShader, normalValueLoc, &normalStrength, SHADER_UNIFORM_FLOAT);
		SetShaderValue(pbrShader, emissiveIntensityLoc, &emissiveIntensity, SHADER_UNIFORM_FLOAT);
		SetShaderValue(pbrShader, reflectionStrengthLoc, &reflectionStrengthValue, SHADER_UNIFORM_FLOAT);

		SetShaderValue(pbrShader, albedoColorLoc, &albedoColor, SHADER_UNIFORM_VEC4);
		SetShaderValue(pbrShader, emissiveColorLoc, &emissiveColor, SHADER_UNIFORM_VEC4);



		bool metallicRoughnessSameTexture =
			hasMetallicMap &&
			hasRoughnessMap &&
			mat.maps[MATERIAL_MAP_METALNESS].texture.id ==
			mat.maps[MATERIAL_MAP_ROUGHNESS].texture.id;

		int useTexAlbedo = hasAlbedoMap ? 1 : 0;
		int useTexNormal = (hasNormalMap && meshHasTangents) ? 1 : 0;
		int useTexEmissive = hasEmissiveMap ? 1 : 0;

		int useTexMRA = metallicRoughnessSameTexture ? 1 : 0;
		int useTexMetallic = 0;
		int useTexRoughness = 0;
		int useTexAO = hasAOMap ? 1 : 0;
		int useGltfMetallicRoughness = metallicRoughnessSameTexture ? 1 : 0;

		if (!metallicRoughnessSameTexture)
		{
			useTexMetallic = hasMetallicMap ? 1 : 0;
			useTexRoughness = hasRoughnessMap ? 1 : 0;
		}

		SetShaderValueIfValid(pbrShader, pbrUseTexAlbedoLoc, &useTexAlbedo, SHADER_UNIFORM_INT);
		SetShaderValueIfValid(pbrShader, pbrUseTexNormalLoc, &useTexNormal, SHADER_UNIFORM_INT);
		SetShaderValueIfValid(pbrShader, pbrUseTexMRALoc, &useTexMRA, SHADER_UNIFORM_INT);
		SetShaderValueIfValid(pbrShader, pbrUseTexEmissiveLoc, &useTexEmissive, SHADER_UNIFORM_INT);

		SetShaderValueIfValid(pbrShader, pbrUseTexMetallicLoc, &useTexMetallic, SHADER_UNIFORM_INT);
		SetShaderValueIfValid(pbrShader, pbrUseTexRoughnessLoc, &useTexRoughness, SHADER_UNIFORM_INT);
		SetShaderValueIfValid(pbrShader, pbrUseTexAOLoc, &useTexAO, SHADER_UNIFORM_INT);
		SetShaderValueIfValid(pbrShader, pbrUseGltfMetallicRoughnessLoc, &useGltfMetallicRoughness, SHADER_UNIFORM_INT);

		SetShaderValueIfValid(pbrShader, pbrAlphaModeLoc, &alphaMode, SHADER_UNIFORM_INT);
		SetShaderValueIfValid(pbrShader, pbrAlphaCutoffLoc, &alphaCutoff, SHADER_UNIFORM_FLOAT);

		int receiveShadows = 1;

		// Held / inspected props are rendered differently, so do not shadow them.
		if (isHeldScenePropRender)
		{
			receiveShadows = 0;
		}

		// Normal transparent glass/hair/etc should not receive projected shadows.
		// Opaque ground-like imported meshes should already have been forced to alphaMode = 0.
		if (transparentPass)
		{
			receiveShadows = 0;
		}

		SetShaderValueIfValid(
			pbrShader,
			pbrReceiveShadowsLoc,
			&receiveShadows,
			SHADER_UNIFORM_INT
		);

		DrawMesh(model.meshes[meshIndex], mat, transform);
	}
}
bool Game::ScenePropHasTransparentMeshes(const SceneProp& prop) const
{
	if (!prop.transparentMaterialIndices.empty())
		return true;

	for (int mode : prop.meshAlphaModes)
	{
		if (mode == 2)
			return true;
	}

	for (int mode : prop.materialAlphaModes)
	{
		if (mode == 2)
			return true;
	}

	return false;
}

void Game::DrawScenePropByIndex(int propIndex) const
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return;

	const SceneProp& prop = sceneProps[propIndex];

	DrawScenePropByIndexPass(propIndex, false);

	if (!ScenePropHasTransparentMeshes(prop))
		return;

	BeginBlendMode(BLEND_ALPHA);
	rlDisableDepthMask();
	rlDisableBackfaceCulling();

	DrawScenePropByIndexPass(propIndex, true);

	rlEnableBackfaceCulling();
	rlEnableDepthMask();
	EndBlendMode();
}

void Game::ApplyScenePropPhysicsModesAfterRebuild()
{
	for (SceneProp& prop : sceneProps)
	{
		if (prop.bodyId.IsInvalid()) continue;
		if (!prop.simulatePhysics) continue;

		physics->SetBodyMotionType(prop.bodyId, JPH::EMotionType::Kinematic);
		physics->SetBodyLinearVelocity(prop.bodyId, { 0.0f, 0.0f, 0.0f });
		physics->SetBodyAngularVelocity(prop.bodyId, { 0.0f, 0.0f, 0.0f });

		prop.editLockPhysics = true;
		prop.syncFromPhysics = false;

		ReadScenePropTransformFromBody(prop);

		if (prop.importedFromGlbScene && prop.model != nullptr)
		{
			SyncImportedEditorOffsetFromRuntime(prop);
		}
		else if (prop.parentIndex == -1)
		{
			SyncScenePropLocalFromWorld(prop);
		}
	}
}

void Game::SyncScenePropLocalFromWorld(SceneProp& prop)
{
	// For root props, local == world.
	// For nested props, this is a temporary simplification.
	// Later we can calculate local = world * inverse(parentWorld).
	prop.localPosition = prop.position;
	prop.localRotationDeg = prop.rotationDeg;
	prop.localScale = prop.scale;
}

void Game::ApplyImportedEditorTransformsToRuntime()
{
	for (SceneProp& prop : sceneProps)
	{
		if (prop.importedFromGlbScene && prop.model != nullptr)
		{
			ApplyImportedEditorTransformToRuntime(prop);
		}
	}
}

void Game::BuildStaticBodiesFromSceneProps()
{
	for (SceneProp& prop : sceneProps)
	{
		prop.bodyId = JPH::BodyID();

		if (!prop.hasCollision)
			continue;

		bool isAggregateProp =
			prop.importedFromGlbScene &&
			(
				ShouldImportedNodeBeAggregateProp(prop.name) ||
				ShouldImportedNodeBeAggregateProp(prop.sourceNodeName)
				);

		// Normal empty transform nodes should not create physics.
		// PROP_ aggregate parents are allowed to create physics even without a mesh.
		if (prop.model == nullptr && !isAggregateProp)
			continue;

		Vector3 center = Vector3Add(prop.position, GetScenePropRotatedOffset(prop));

		bool rotated =
			fabsf(prop.rotationDeg.x) > 0.01f ||
			fabsf(prop.rotationDeg.y) > 0.01f ||
			fabsf(prop.rotationDeg.z) > 0.01f;

		PhysicsBody body;
		body.type = prop.simulatePhysics ? BodyType::Dynamic : BodyType::Static;
		body.position = center;
		body.rotationDeg = prop.rotationDeg;
		body.blocksPlayer = prop.blocksPlayer;
		body.velocity = { 0.0f, 0.0f, 0.0f };

		Vector3 scaledCollider = {
			fabsf(prop.colliderSize.x * prop.scale.x),
			fabsf(prop.colliderSize.y * prop.scale.y),
			fabsf(prop.colliderSize.z * prop.scale.z)
		};

		body.halfExtents = {
			scaledCollider.x * 0.5f,
			scaledCollider.y * 0.5f,
			scaledCollider.z * 0.5f
		};

		if (!IsUsableJoltHalfExtents(body.halfExtents))
		{
			TraceLog(
				LOG_WARNING,
				"Skipping invalid Jolt collider for prop '%s': colliderSize=(%.4f %.4f %.4f), scale=(%.4f %.4f %.4f), half=(%.4f %.4f %.4f)",
				prop.name.c_str(),
				prop.colliderSize.x, prop.colliderSize.y, prop.colliderSize.z,
				prop.scale.x, prop.scale.y, prop.scale.z,
				body.halfExtents.x, body.halfExtents.y, body.halfExtents.z
			);

			prop.bodyId = JPH::BodyID();
			continue;
		}

		body.invMass = 0.0f;
		body.useGravity = false;
		body.isTrigger = false;
		body.isActive = true;
		if (prop.importedFromGlbScene)
		{
			body.useJoltCollider = prop.useJoltCollider || prop.simulatePhysics;
		}
		else
		{
			body.useJoltCollider = prop.useJoltCollider || rotated || prop.simulatePhysics;
		}

		if (prop.useNormalCollision && !rotated && !prop.simulatePhysics)
			if (prop.useNormalCollision && !rotated && !prop.simulatePhysics)
			{
				staticBodies.emplace_back(body);
			}

		if (body.useJoltCollider)
		{
			Quaternion rot = QuaternionFromEuler(
				prop.rotationDeg.x * DEG2RAD,
				prop.rotationDeg.y * DEG2RAD,
				prop.rotationDeg.z * DEG2RAD
			);

			if (prop.simulatePhysics)
			{
				prop.bodyId = physics->AddDynamicBox(center, body.halfExtents);
				physics->SetBodyRotation(prop.bodyId, rot);
			}
			else
			{
				prop.bodyId = physics->AddStaticBox(center, body.halfExtents, rot);
			}
		}
	}
}

void Game::ApplyEnvironmentCubemap(Model& model)
{
	Texture cubemapTex = skyboxModel.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture;

	for (int i = 0; i < model.materialCount; i++)
	{
		model.materials[i].shader = pbrShader;
		model.materials[i].maps[MATERIAL_MAP_CUBEMAP].texture = cubemapTex;
	}
}

void Game::LoadSkybox(const char* fileName, bool useHDR)
{
	Mesh cube = GenMeshCube(1.0f, 1.0f, 1.0f);
	skyboxModel = LoadModelFromMesh(cube);


	skyboxShader = LoadShader("Shaders/skybox.vs", "Shaders/skybox.fs");
	skyboxModel.materials[0].shader = skyboxShader;

	int environmentMap = MATERIAL_MAP_CUBEMAP;
	int doGamma = useHDR ? 1 : 0;
	int vflipped = useHDR ? 1 : 0;

	SetShaderValue(skyboxShader, GetShaderLocation(skyboxShader, "environmentMap"), &environmentMap, SHADER_UNIFORM_INT);
	SetShaderValue(skyboxShader, GetShaderLocation(skyboxShader, "doGamma"), &doGamma, SHADER_UNIFORM_INT);
	SetShaderValue(skyboxShader, GetShaderLocation(skyboxShader, "vflipped"), &vflipped, SHADER_UNIFORM_INT);

	if (useHDR)
	{
		cubemapShader = LoadShader("Shaders/cubemap.vs", "Shaders/cubemap.fs");

		int equirectangularMap = 0;
		SetShaderValue(cubemapShader, GetShaderLocation(cubemapShader, "equirectangularMap"), &equirectangularMap, SHADER_UNIFORM_INT);

		Texture2D panorama = LoadTexture(fileName);

		if (panorama.id == 0)
		{
			TraceLog(LOG_ERROR, "Skybox HDR failed to load: %s", fileName);
			return;

		}
		skyboxModel.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture =
			GenTextureCubemap(cubemapShader, panorama, 1024, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

		UnloadTexture(panorama);
	}
	else
	{
		Image image = LoadImage(fileName);
		skyboxModel.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture =
			LoadTextureCubemap(image, CUBEMAP_LAYOUT_AUTO_DETECT);
		UnloadImage(image);
	}

	skyboxLoaded = true;
}

void Game::DrawSkybox() const
{
	if (!skyboxLoaded) return;

	rlDisableBackfaceCulling();
	rlDisableDepthMask();
	DrawModel(skyboxModel, { 0.0f, 0.0f, 0.0f }, 1.0f, WHITE);
	DrawModel(skyboxModel, { 0.0f, 0.0f, 0.0f }, 1.0f, WHITE);
	rlEnableBackfaceCulling();
	rlEnableDepthMask();
}

void Game::UnloadSkybox()
{
	if (!skyboxLoaded) return;

	UnloadTexture(skyboxModel.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture);
	UnloadShader(skyboxShader);

	if (cubemapShader.id != 0)
		UnloadShader(cubemapShader);

	UnloadModel(skyboxModel);
	skyboxLoaded = false;
}

//dialogue

void Game::RefreshDialogueUILayout()
{
	float screenW = (float)GetScreenWidth();
	float screenH = (float)GetScreenHeight();

	if (screenW <= 0.0f || screenH <= 0.0f)
		return;

	const float marginX = 60.0f;
	const float panelH = 130.0f;
	const float panelBottomMargin = 50.0f;

	const float labelMarginX = 90.0f;
	const float labelH = 70.0f;
	const float labelTopPadding = 30.0f;

	Rectangle panelRect = {
		marginX,
		screenH - panelH - panelBottomMargin,
		screenW - marginX * 2.0f,
		panelH
	};

	Rectangle labelRect = {
		labelMarginX,
		panelRect.y + labelTopPadding,
		screenW - labelMarginX * 2.0f,
		labelH
	};

	// Safety clamp for smaller screens/windows.
	if (panelRect.width < 200.0f)
		panelRect.width = 200.0f;

	if (labelRect.width < 160.0f)
		labelRect.width = 160.0f;

	panelRect.y = Clamp(panelRect.y, 20.0f, screenH - panelRect.height - 20.0f);
	labelRect.y = Clamp(labelRect.y, 30.0f, screenH - labelRect.height - 30.0f);

	if (customerDialoguePanel != nullptr)
	{
		customerDialoguePanel->bounds = panelRect;
	}

	if (customerDialogueLabel != nullptr)
	{
		customerDialogueLabel->bounds = labelRect;
	}
}

void Game::BuildDialogue()
{
	customerDialogueRoot = CreateDialogueNode("customer_intro", "Customer Dialogue");

	RayDialComponent* panel = CreatePanel(
		Rectangle{
			60.0f,
			(float)GetScreenHeight() - 180.0f,
			(float)GetScreenWidth() - 120.0f,
			130.0f
		},
		Color{ 20, 20, 25, 230 }
	);

	customerDialoguePanel = panel;
	RayDialComponent* label = CreateLabel(
		Rectangle{
			90.0f,
			(float)GetScreenHeight() - 150.0f,
			(float)GetScreenWidth() - 180.0f,
			70.0f
		},
		"",
		true
	);

	customerDialogueLabel = label;

	RayDialLabelData* labelData = (RayDialLabelData*)label->data;
	labelData->textColor = WHITE;
	labelData->fontSize = 20;

	AddComponent(panel, label);
	customerDialogueRoot->components = panel;

	customerDialogueManager = CreateDialogueManager(customerDialogueRoot);
}

void Game::CompleteDialogueAndClose(const DialogueNode& endingNode)
{
	int completedCustomerIndex = activeDialogueCustomerIndex;
	std::string completedScriptId = activeDialogueScriptId;
	bool continuesToNextScript = !endingNode.nextScriptOnEnd.empty();

	if (activeDialogueCustomerIndex >= 0 &&
		activeDialogueCustomerIndex < (int)customers.size())
	{
		Customer& customer = customers[activeDialogueCustomerIndex];

		if (!endingNode.nextScriptOnEnd.empty())
		{
			if (dialogueScripts.find(endingNode.nextScriptOnEnd) != dialogueScripts.end())
			{
				customer.dialogueScriptId = endingNode.nextScriptOnEnd;

				TraceLog(
					LOG_INFO,
					"Customer dialogue advanced to script: %s",
					customer.dialogueScriptId.c_str()
				);
			}
			else
			{
				TraceLog(
					LOG_WARNING,
					"Next dialogue script does not exist: %s",
					endingNode.nextScriptOnEnd.c_str()
				);
			}
		}
	}

	CloseDialogue();

	if (!continuesToNextScript)
	{
		OnCustomerDialogueCompleted(
			completedCustomerIndex,
			completedScriptId
		);
	}
}

void Game::BuildCustomerDialogueTree()
{
	dialogueScripts.clear();

	// =========================================================
	// SELLER CUSTOMER - PART 1
	// Intro + first branching choice.
	// After this ends, the customer continues with seller_part2
	// the next time the player talks to them.
	// =========================================================

	DialogueScript sellerPart1;
	sellerPart1.id = "seller_part1";

	constexpr int SELLER_P1_INTRO = 0;
	constexpr int SELLER_P1_MAIN_CHOICE = 1;
	constexpr int SELLER_P1_INSPECT_ASK = 2;
	constexpr int SELLER_P1_INSPECT_REPLY = 3;
	constexpr int SELLER_P1_PRICE_ASK = 4;
	constexpr int SELLER_P1_PRICE_REPLY = 5;
	constexpr int SELLER_P1_REFUSE = 6;
	constexpr int SELLER_P1_MERGE_APPRAISAL = 7;
	constexpr int SELLER_P1_END = 8;

	sellerPart1.nodes = {
		// 0 - Intro
		{
			"Customer",
			"Hi. I brought something I want to sell.",
			SELLER_P1_MAIN_CHOICE,
			false,
			{},
			"Audio/customer_sell_intro.mp3",
			CustomerAnimState::Idle,
			true,
			""
		},

		// 1 - Main choice
		{
			"Player",
			"Sure. What would you like me to do first?",
			-1,
			false,
			{
				{ "Ask to inspect the item.", SELLER_P1_INSPECT_ASK },
				{ "Ask what price they want.", SELLER_P1_PRICE_ASK },
				{ "Refuse politely.", SELLER_P1_REFUSE }
			},
			"",
			CustomerAnimState::LeftHand,
			true,
			""
		},

		// 2 - Inspect branch starts
		{
			"Player",
			"Can I inspect the item first?",
			SELLER_P1_INSPECT_REPLY,
			false,
			{},
			"",
			CustomerAnimState::Dance,
			true,
			""
		},

		// 3 - Inspect branch response
		{
			"Customer",
			"Of course. It is an old handheld console. I am not sure if it still works.",
			SELLER_P1_MERGE_APPRAISAL,
			false,
			{},
			"Audio/customer_console.wav",
			CustomerAnimState::Twerk,
			true,
			""
		},

		// 4 - Price branch starts
		{
			"Player",
			"How much were you hoping to get for it?",
			SELLER_P1_PRICE_REPLY,
			false,
			{},
			"",
			CustomerAnimState::Think,
			true,
			""
		},

		// 5 - Price branch response
		{
			"Customer",
			"I was hoping to get a fair price, but I am open to your offer.",
			SELLER_P1_MERGE_APPRAISAL,
			false,
			{},
			"Audio/customer_price.wav",
			CustomerAnimState::Point,
			true,
			""
		},

		// 6 - Refuse branch ends separately
		{
			"Customer",
			"Oh... alright. Maybe next time.",
			-1,
			true,
			{},
			"Audio/customer_rejected.wav",
			CustomerAnimState::Idle,
			true,
			"seller_refused"
		},

		// 7 - Merge point for inspect branch and price branch
		{
			"Player",
			"Alright, I can appraise it and make you an offer.",
			SELLER_P1_END,
			false,
			{},
			"",
			CustomerAnimState::Idle,
			false,
			""
		},

		// 8 - Part 1 complete
		{
			"Customer",
			"Thank you. I will wait while you check it.",
			-1,
			true,
			{},
			"Audio/customer_thank_you.wav",
			CustomerAnimState::LeftHand,
			true,
			"seller_part2"
		}
	};

	dialogueScripts[sellerPart1.id] = sellerPart1;

	// =========================================================
	// SELLER CUSTOMER - PART 2
	// Starts when the player talks to the same customer again.
	// Example purpose: after the player has inspected the item.
	// =========================================================

	DialogueScript sellerPart2;
	sellerPart2.id = "seller_part2";

	constexpr int SELLER_P2_INTRO = 0;
	constexpr int SELLER_P2_CHOICE = 1;
	constexpr int SELLER_P2_GOOD_CONDITION = 2;
	constexpr int SELLER_P2_BAD_CONDITION = 3;
	constexpr int SELLER_P2_MERGE_OFFER = 4;
	constexpr int SELLER_P2_END = 5;

	sellerPart2.nodes = {
		// 0 - Part 2 intro
		{
			"Customer",
			"So, what do you think? Is it worth anything?",
			SELLER_P2_CHOICE,
			false,
			{},
			"Audio/customer_ask_value.wav",
			CustomerAnimState::LeftLook,
			true,
			""
		},

		// 1 - Player condition choice
		{
			"Player",
			"I checked the item. Here is what I found.",
			-1,
			false,
			{
				{ "Say the item is in decent condition.", SELLER_P2_GOOD_CONDITION },
				{ "Say the item has some problems.", SELLER_P2_BAD_CONDITION }
			},
			"",
			CustomerAnimState::Idle,
			true,
			""
		},

		// 2 - Good condition branch
		{
			"Player",
			"The item is in decent condition. It should still have value.",
			SELLER_P2_MERGE_OFFER,
			false,
			{},
			"",
			CustomerAnimState::Idle,
			false,
			""
		},

		// 3 - Bad condition branch
		{
			"Player",
			"It has some issues, so I cannot offer the full amount.",
			SELLER_P2_MERGE_OFFER,
			false,
			{},
			"",
			CustomerAnimState::Idle,
			false,
			""
		},

		// 4 - Merge point
		{
			"Customer",
			"I understand. What can you offer for it?",
			SELLER_P2_END,
			false,
			{},
			"Audio/customer_ask_offer.wav",
			CustomerAnimState::Idle,
			true,
			""
		},

		// 5 - Part 2 complete
		{
			"Player",
			"Let me prepare the offer.",
			-1,
			true,
			{},
			"",
			CustomerAnimState::Idle,
			false,
			"seller_part3"
		}
	};

	dialogueScripts[sellerPart2.id] = sellerPart2;

	// =========================================================
	// SELLER CUSTOMER - PART 3
	// Final offer conversation.
	// =========================================================

	DialogueScript sellerPart3;
	sellerPart3.id = "seller_part3";

	constexpr int SELLER_P3_OFFER = 0;
	constexpr int SELLER_P3_CHOICE = 1;
	constexpr int SELLER_P3_ACCEPT = 2;
	constexpr int SELLER_P3_DECLINE = 3;

	sellerPart3.nodes = {
		// 0 - Offer
		{
			"Player",
			"I can offer 3,000 yen for it.",
			SELLER_P3_CHOICE,
			false,
			{},
			"",
			CustomerAnimState::Idle,
			false,
			""
		},

		// 1 - Customer counter choice
		{
			"Customer",
			"3,000 yen... Can you do any better?",
			-1,
			false,
			{
				{ "Keep the offer at 3,000 yen.", SELLER_P3_ACCEPT },
				{ "Raise the offer to 3,500 yen.", SELLER_P3_ACCEPT },
				{ "Cancel the deal.", SELLER_P3_DECLINE }
			},
			"Audio/customer_counter_offer.wav",
			CustomerAnimState::Emote,
			true,
			""
		},

		// 2 - Accept deal
		{
			"Customer",
			"Alright. I will accept the offer.",
			-1,
			true,
			{},
			"Audio/customer_accept_offer.wav",
			CustomerAnimState::Emote,
			true,
			"seller_finished"
		},

		// 3 - Decline deal
		{
			"Customer",
			"I see. Maybe I will keep it for now.",
			-1,
			true,
			{},
			"Audio/customer_decline_offer.wav",
			CustomerAnimState::Idle,
			true,
			"seller_finished"
		}
	};

	dialogueScripts[sellerPart3.id] = sellerPart3;

	// =========================================================
	// SELLER REFUSED FOLLOW-UP
	// Used if the player refused the customer in Part 1.
	// =========================================================

	DialogueScript sellerRefused;
	sellerRefused.id = "seller_refused";

	sellerRefused.nodes = {
		{
			"Customer",
			"I understand. I will look around for now.",
			-1,
			true,
			{},
			"",
			CustomerAnimState::Idle,
			true,
			""
		}
	};

	dialogueScripts[sellerRefused.id] = sellerRefused;

	// =========================================================
	// SELLER FINISHED FOLLOW-UP
	// Used after the deal is accepted or cancelled.
	// =========================================================

	DialogueScript sellerFinished;
	sellerFinished.id = "seller_finished";

	sellerFinished.nodes = {
		{
			"Customer",
			"Thank you for your time.",
			-1,
			true,
			{},
			"",
			CustomerAnimState::Idle,
			true,
			""
		}
	};

	dialogueScripts[sellerFinished.id] = sellerFinished;

	// =========================================================
	// BROWSER CUSTOMER
	// Simple one-part browsing dialogue.
	// =========================================================

	DialogueScript browser;
	browser.id = "browser_customer";

	constexpr int BROWSER_INTRO = 0;
	constexpr int BROWSER_CHOICE = 1;
	constexpr int BROWSER_RETRO_GAMES = 2;
	constexpr int BROWSER_GACHA = 3;
	constexpr int BROWSER_BROWSE = 4;

	browser.nodes = {
		// 0 - Intro
		{
			"Customer",
			"Hello. I am just looking around.",
			BROWSER_CHOICE,
			false,
			{},
			"Audio/customer_browse_intro.wav",
			CustomerAnimState::Idle,
			true,
			""
		},

		// 1 - Player choice
		{
			"Player",
			"No problem. Looking for anything specific?",
			-1,
			false,
			{
				{ "Recommend retro games.", BROWSER_RETRO_GAMES },
				{ "Mention the gacha machine.", BROWSER_GACHA },
				{ "Let them browse.", BROWSER_BROWSE }
			},
			"",
			CustomerAnimState::Idle,
			false,
			""
		},

		// 2 - Retro games branch
		{
			"Customer",
			"Retro games sound interesting. What do you recommend?",
			-1,
			true,
			{},
			"",
			CustomerAnimState::Idle,
			true,
			"browser_finished"
		},

		// 3 - Gacha branch
		{
			"Customer",
			"That machine looks fun. I might try it.",
			-1,
			true,
			{},
			"",
			CustomerAnimState::Idle,
			true,
			"browser_finished"
		},

		// 4 - Let them browse branch
		{
			"Customer",
			"Thanks. I will take a look first.",
			-1,
			true,
			{},
			"",
			CustomerAnimState::Idle,
			true,
			"browser_finished"
		}
	};

	dialogueScripts[browser.id] = browser;

	// =========================================================
	// BROWSER FINISHED FOLLOW-UP
	// =========================================================

	DialogueScript browserFinished;
	browserFinished.id = "browser_finished";

	browserFinished.nodes = {
		{
			"Customer",
			"I am still looking around.",
			-1,
			true,
			{},
			"",
			CustomerAnimState::Idle,
			true,
			""
		}
	};
	dialogueScripts[browserFinished.id] = browserFinished;

	// =========================================================
	// GENERIC BROWSER COUNTER PURCHASE FALLBACK
	// Used only when a buyer somehow has no tradeItemId.
	// Item-specific buyer scripts are generated below.
	// =========================================================

	DialogueScript browserCounter;
	browserCounter.id = "browser_counter_purchase";

	browserCounter.nodes = {
		{
		"Customer",
		"I found something I want to buy.",
		1,
		false,
		{},
		VoicePath(browserCounter.id, 0),
		CustomerAnimState::Give,
		true,
		"",
		DialogueAction::PlaceCounterItem
	},
	{
		"Player",
		"Sure. Let me scan it first.",
		2,
		false,
		{},
		VoicePath(browserCounter.id, 1),
		CustomerAnimState::Idle,
		false,
		""
	},
	{
		"Customer",
		"I will wait while you scan it.",
		-1,
		true,
		{},
		VoicePath(browserCounter.id, 2),
		CustomerAnimState::Idle,
		true,
		""
	}
	};

	dialogueScripts[browserCounter.id] = browserCounter;

	// =========================================================
	// ITEM-SPECIFIC SELLER / BUYER DIALOGUES
	// =========================================================

	for (const CustomerTradeItemDef& item : customerTradeItems)
	{
		AddSellerDialogueScriptsForItem(item);
		AddBuyerDialogueScriptForItem(item);
	}
}

void Game::AddSellerDialogueScriptsForItem(
	const CustomerTradeItemDef& item
)
{
	std::string part1Id = GetSellerScriptIdForItem(item.id, "part1");
	std::string part2Id = GetSellerScriptIdForItem(item.id, "part2");
	std::string part3Id = GetSellerScriptIdForItem(item.id, "part3");

	// --------------------------
	// Seller item-specific part 1
	// --------------------------
	DialogueScript part1;
	part1.id = part1Id;

	part1.nodes = {
{
	"Customer",
	item.sellerIntroLine,
	1,
	false,
	{},
	VoicePath(part1.id, 0),
	CustomerAnimState::Give,
	true,
	"",
	DialogueAction::PlaceCounterItem
},
{
	"Player",
	"Sure. Place it on the counter and I will inspect it.",
	2,
	false,
	{},
	SharedVoicePath("seller_shared_place_counter"),
	CustomerAnimState::Idle,
	false,
	""
},
{
	"Customer",
	item.sellerConditionLine,
	-1,
	true,
	{},
	VoicePath(part1.id, 2),
	CustomerAnimState::LeftHand,
	true,
	part2Id
}
	};

	dialogueScripts[part1.id] = part1;

	// --------------------------
	// Seller item-specific part 2
	// --------------------------
	DialogueScript part2;
	part2.id = part2Id;

	part2.nodes = {
		{
	"Customer",
	"So, what is the trade-in value?",
	1,
	false,
	{},
	SharedVoicePath("seller_shared_trade_value"),
	CustomerAnimState::LeftLook,
	true,
	""
},
{
	"Player",
	"I checked the item condition.",
	-1,
	false,
	{
		{ "Say it is in good condition.", 2 },
		{ "Say it has some problems.", 3 }
	},
	SharedVoicePath("seller_shared_checked_condition"),
	CustomerAnimState::Idle,
	false,
	""
},
{
	"Player",
	"The condition looks acceptable, so I can make an offer.",
	4,
	false,
	{},
	SharedVoicePath("seller_shared_condition_good"),
	CustomerAnimState::Idle,
	false,
	""
},
{
	"Player",
	"There are some condition issues, so I need to keep the offer lower.",
	4,
	false,
	{},
	SharedVoicePath("seller_shared_condition_issues"),
	CustomerAnimState::Idle,
	false,
	""
},
{
	"Customer",
	item.sellerPriceHopeLine,
	-1,
	true,
	{},
	VoicePath(part2.id, 4),
	CustomerAnimState::Think,
	true,
	part3Id
}
	};

	dialogueScripts[part2.id] = part2;

	// --------------------------
	// Seller item-specific part 3
	// --------------------------
	DialogueScript part3;
	part3.id = part3Id;

	int buyPrice = GetSellerBuyPriceYen(item);

	std::string offerText = TextFormat(
		"I can offer %i yen cash for the %s.",
		buyPrice,
		item.displayName.c_str()
	);

	std::string counterText = TextFormat(
		"%i yen... Alright, that sounds fair.",
		buyPrice
	);

	part3.nodes = {
		{
	"Player",
	offerText,
	1,
	false,
	{},
	VoicePath(part3.id, 0),
	CustomerAnimState::Idle,
	false,
	""
},
{
	"Customer",
	counterText,
	-1,
	false,
	{
		{ "Confirm the offer.", 2 },
		{ "Cancel the deal.", 3 }
	},
	VoicePath(part3.id, 1),
	CustomerAnimState::Emote,
	true,
	""
},
{
	"Customer",
	"Alright. I will accept the offer. Thank you.",
	-1,
	true,
	{},
	SharedVoicePath("seller_shared_accept_offer"),
	CustomerAnimState::Emote,
	true,
	"",
	DialogueAction::SellerPurchaseAccepted
},
{
	"Customer",
	"I see. Maybe I will keep it for now.",
	-1,
	true,
	{},
	SharedVoicePath("seller_shared_reject_offer"),
	CustomerAnimState::Give,
	true,
	"",
	DialogueAction::SellerPurchaseDeclined
}
	};

	dialogueScripts[part3.id] = part3;
}

void Game::AddBuyerDialogueScriptForItem(
	const CustomerTradeItemDef& item
)
{
	DialogueScript script;
	script.id = GetBuyerCounterScriptIdForItem(item.id);

	std::string scanText = TextFormat(
		"Sure. I will scan the %s first.",
		item.displayName.c_str()
	);

	std::string waitText =
		"Okay, I will wait while you scan it.";

	script.nodes = {
		{
		"Customer",
		item.buyerCounterLine,
		1,
		false,
		{},
		VoicePath(script.id, 0),
		CustomerAnimState::Give,
		true,
		"",
		DialogueAction::PlaceCounterItem
	},
	{
		"Player",
		scanText,
		2,
		false,
		{},
		VoicePath(script.id, 1),
		CustomerAnimState::Idle,
		false,
		""
	},
	{
		"Customer",
		waitText,
		-1,
		true,
		{},
		SharedVoicePath("buyer_shared_wait_scan"),
		CustomerAnimState::Idle,
		true,
		""
	}
	};

	dialogueScripts[script.id] = script;
}


int Game::FindTalkableCustomer()
{
	RebuildItemPlacementSpotOccupancy();
	blockedInteractionPrompt.clear();

	Ray ray{};
	ray.position = camera.position;
	ray.direction = Vector3Normalize(
		Vector3Subtract(camera.target, camera.position)
	);

	auto IsCustomerRayTalkable = [&](int customerIndex, float& outHitDistance) -> bool
		{
			if (customerIndex < 0 || customerIndex >= (int)customers.size())
				return false;

			const Customer& customer = customers[customerIndex];

			float distToCustomer = Vector3Distance(player.m_pos, customer.position);

			if (distToCustomer > customerTalkDistance)
				return false;

			BoundingBox talkBox = customer.GetTalkBounds();
			RayCollision hit = GetRayCollisionBox(ray, talkBox);

			if (!hit.hit)
				return false;

			if (hit.distance > customerTalkDistance)
				return false;

			outHitDistance = hit.distance;
			return true;
		};

	// ----------------------------------------------------
	// 1. Priority pass:
	// Only front counter customers are interactable.
	// If the counter is blocked, store the blocked prompt.
	// ----------------------------------------------------
	int bestCounterIndex = -1;
	float bestCounterHitDistance = customerTalkDistance;

	float bestBlockedHitDistance = customerTalkDistance;
	std::string bestBlockedReason = "";

	for (int i = 0; i < (int)customers.size(); i++)
	{
		const Customer& customer = customers[i];

		if (!IsCounterCustomerState(customer.aiState))
			continue;

		if (!IsCustomerAtFrontQueueSlot(i))
			continue;

		float hitDistance = 0.0f;

		if (!IsCustomerRayTalkable(i, hitDistance))
			continue;

		std::string blockedReason;

		if (!CanTalkToCustomerNow(i, &blockedReason))
		{
			if (!blockedReason.empty() &&
				hitDistance < bestBlockedHitDistance)
			{
				bestBlockedHitDistance = hitDistance;
				bestBlockedReason = blockedReason;
			}

			continue;
		}

		if (hitDistance < bestCounterHitDistance)
		{
			bestCounterHitDistance = hitDistance;
			bestCounterIndex = i;
		}
	}

	if (bestCounterIndex >= 0)
		return bestCounterIndex;

	if (!bestBlockedReason.empty())
	{
		blockedInteractionPrompt = bestBlockedReason;
		return -1;
	}

	// ----------------------------------------------------
	// 2. Optional fallback:
	// Existing non-queue customer talk behavior.
	// Queue customers are skipped so queue slot 1/2/3 are not interactable.
	// ----------------------------------------------------
	int bestIndex = -1;
	float bestHitDistance = customerTalkDistance;

	for (int i = 0; i < (int)customers.size(); i++)
	{
		const Customer& customer = customers[i];

		if (IsQueueCustomerState(customer.aiState))
			continue;

		if (!customer.CanTalk())
			continue;

		float hitDistance = 0.0f;

		if (!IsCustomerRayTalkable(i, hitDistance))
			continue;

		if (hitDistance < bestHitDistance)
		{
			bestHitDistance = hitDistance;
			bestIndex = i;
		}
	}

	return bestIndex;
}

void Game::CloseDialogue()
{
	if (activeDialogueCustomerIndex >= 0)
	{
		EndCustomerDialogueFocus();
	}

	if (hasDialogueVoice)
	{
		StopSound(currentDialogueVoice);
		UnloadSound(currentDialogueVoice);
		hasDialogueVoice = false;
	}

	StopDialogueFallbackBeep();

	if (activeDialogueCustomerIndex >= 0 &&
		activeDialogueCustomerIndex < (int)customers.size())
	{
		customers[activeDialogueCustomerIndex].EndDialogue();
	}

	dialogueActive = false;
	dialogueCustomerIndex = -1;
	activeDialogueCustomerIndex = -1;
	activeDialogueScriptId = "";

	currentDialogueNode = 0;
	selectedDialogueChoice = 0;

	currentDialogueAutoPlayEnabled = false;
	currentDialogueHasValidVoice = false;
	dialogueVoiceFinished = false;
	dialogueAutoAdvanceTimer = 0.0f;

	currentDialogueText.clear();
	fullDialogueText.clear();
	visibleDialogueText.clear();
	dialogueTextComplete = true;

	SetDialogueLabelText("");
}

void Game::SetDialogueNode(int nodeIndex)
{
	DialogueScript* script = GetActiveDialogueScript();

	if (script == nullptr)
	{
		TraceLog(
			LOG_WARNING,
			"Dialogue script not found: %s",
			activeDialogueScriptId.c_str()
		);

		CloseDialogue();
		return;
	}

	if (nodeIndex < 0 || nodeIndex >= (int)script->nodes.size())
	{
		TraceLog(
			LOG_WARNING,
			"Dialogue node index out of range: %i",
			nodeIndex
		);

		CloseDialogue();
		return;
	}

	currentDialogueNode = nodeIndex;
	selectedDialogueChoice = 0;

	DialogueNode& node = script->nodes[currentDialogueNode];

	// Stop previous node audio/beep state first.
	StopDialogueFallbackBeep();

	dialogueAutoAdvanceTimer = 0.0f;
	dialogueVoiceFinished = false;
	currentDialogueHasValidVoice = false;

	currentDialogueText = node.speaker + ": " + node.text;

	StartDialogueTextAnimation(currentDialogueText, 35.0f);

	if (activeDialogueCustomerIndex >= 0 &&
		activeDialogueCustomerIndex < (int)customers.size() &&
		node.applyCustomerAnim)
	{
		customers[activeDialogueCustomerIndex].ApplyDialogueCue(
			node.customerAnim
		);
	}

	RunDialogueAction(node);

	currentDialogueAutoPlayEnabled = IsCurrentDialogueAutoPlayEnabled();

	currentDialogueHasValidVoice =
		PlayDialogueVoiceIfAvailable(node.voicePath);

	if (!currentDialogueHasValidVoice)
	{
		StartDialogueFallbackBeepIfNeeded();
	}

	if (!node.sfxId.empty())
	{
		PlaySfx(node.sfxId, true);
	}
}

bool Game::IsCurrentDialogueSelfDialogue() const
{
	return dialogueActive &&
		activeDialogueCustomerIndex < 0;
}

bool Game::IsCurrentDialogueCustomerDialogue() const
{
	return dialogueActive &&
		activeDialogueCustomerIndex >= 0;
}

bool Game::IsCurrentDialogueAutoPlayEnabled() const
{
	if (!dialogueActive)
		return false;

	if (IsCurrentDialogueCustomerDialogue())
		return customerDialogueAutoPlayEnabled;

	// Self-dialogue and inspect self-dialogue autoplay by default.
	return true;
}

bool Game::AdvanceDialogueFromCurrentNode()
{
	DialogueNode* node = GetActiveDialogueNode();

	if (node == nullptr)
	{
		CloseDialogue();
		return true;
	}

	if (node->endDialogue)
	{
		CompleteDialogueAndClose(*node);
		return true;
	}

	if (!node->choices.empty())
	{
		if (selectedDialogueChoice < 0 ||
			selectedDialogueChoice >= (int)node->choices.size())
		{
			selectedDialogueChoice = 0;
		}

		SetDialogueNode(node->choices[selectedDialogueChoice].nextNode);
		return true;
	}

	SetDialogueNode(node->nextNode);
	return true;
}

void Game::UpdateDialogueAutoPlay(float dt)
{
	if (!dialogueActive)
		return;

	if (!currentDialogueAutoPlayEnabled)
		return;

	DialogueNode* node = GetActiveDialogueNode();

	if (node == nullptr)
		return;

	// Do not auto-select choices.
	if (!node->choices.empty())
		return;

	if (currentDialogueHasValidVoice && hasDialogueVoice)
	{
		if (IsSoundPlaying(currentDialogueVoice))
		{
			dialogueAutoAdvanceTimer = 0.0f;
			return;
		}

		if (!dialogueVoiceFinished)
		{
			dialogueVoiceFinished = true;
			dialogueAutoAdvanceTimer = 0.0f;
		}

		dialogueAutoAdvanceTimer += dt;

		if (dialogueAutoAdvanceTimer >= dialogueAutoVoicePostDelay)
		{
			AdvanceDialogueFromCurrentNode();
		}

		return;
	}

	// No valid voice file:
	// wait for text animation to complete, then wait 2 seconds.
	if (!dialogueTextComplete)
	{
		dialogueAutoAdvanceTimer = 0.0f;
		return;
	}

	dialogueAutoAdvanceTimer += dt;

	if (dialogueAutoAdvanceTimer >= dialogueAutoTextPostDelay)
	{
		AdvanceDialogueFromCurrentNode();
	}
}

std::string Game::GetInspectSfxForTag(const std::string& tag) const
{
	std::string cleanTag = SanitizeVoiceToken(tag);

	if (cleanTag == "cash_register")
		return "cash";

	if (cleanTag == "scanner")
		return "scan";

	if (cleanTag == "amongus")
		return "amongus";

	if (cleanTag == "ultrakillha")
		return "ultrakillHA";

	if (cleanTag == "music_box")
		return "music_box";

	if (cleanTag == "forehead")
		return "forehead";

	if (cleanTag == "invisible")
		return "invisible";

	return "";
}

static std::string ToLowerCopy(std::string s)
{
	std::transform(
		s.begin(),
		s.end(),
		s.begin(),
		[](unsigned char c) { return (char)std::tolower(c); }
	);

	return s;
}

int Game::FindScenePropByNodeNameExact(const std::string& nodeName) const
{
	std::string wanted = ToLowerCopy(nodeName);

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		const SceneProp& prop = sceneProps[i];

		if (ToLowerCopy(prop.name) == wanted)
			return i;

		if (ToLowerCopy(prop.sourceNodeName) == wanted)
			return i;
	}

	return -1;
}

void Game::UnloadRecordPlayerTrack()
{
	if (recordPlayerMusicLoaded)
	{
		StopMusicStream(recordPlayerMusic);
		UnloadMusicStream(recordPlayerMusic);
		recordPlayerMusicLoaded = false;
	}
}

static int ReadBE32(const unsigned char* p)
{
	return
		((int)p[0] << 24) |
		((int)p[1] << 16) |
		((int)p[2] << 8) |
		((int)p[3]);
}

static int ReadSynchsafe32(const unsigned char* p)
{
	return
		((int)(p[0] & 0x7F) << 21) |
		((int)(p[1] & 0x7F) << 14) |
		((int)(p[2] & 0x7F) << 7) |
		((int)(p[3] & 0x7F));
}

static std::vector<unsigned char> ReadWholeBinaryFile(const std::string& path)
{
	std::ifstream file(path, std::ios::binary);

	if (!file.is_open())
		return {};

	file.seekg(0, std::ios::end);
	std::streamoff size = file.tellg();
	file.seekg(0, std::ios::beg);

	if (size <= 0)
		return {};

	std::vector<unsigned char> bytes((size_t)size);
	file.read((char*)bytes.data(), size);

	return bytes;
}

static std::string StripPathAndExtensionForMusic(const std::string& path)
{
	std::string p = path;

	std::replace(p.begin(), p.end(), '\\', '/');

	size_t slash = p.find_last_of('/');
	size_t start = slash == std::string::npos ? 0 : slash + 1;

	size_t dot = p.find_last_of('.');

	if (dot == std::string::npos || dot < start)
		dot = p.size();

	return p.substr(start, dot - start);
}

static std::string TrimNullsAndSpaces(std::string s)
{
	while (!s.empty() && (s.back() == '\0' || s.back() == ' ' || s.back() == '\r' || s.back() == '\n' || s.back() == '\t'))
		s.pop_back();

	size_t start = 0;

	while (start < s.size() && (s[start] == '\0' || s[start] == ' ' || s[start] == '\r' || s[start] == '\n' || s[start] == '\t'))
		start++;

	if (start > 0)
		s.erase(0, start);

	return s;
}

static std::string DecodeID3TextFrame(const unsigned char* data, int len)
{
	if (data == nullptr || len <= 1)
		return "";

	unsigned char encoding = data[0];

	const unsigned char* text = data + 1;
	int textLen = len - 1;

	// 0 = ISO-8859-1, 3 = UTF-8.
	// For your current English metadata, both are safe to treat as normal bytes.
	if (encoding == 0 || encoding == 3)
	{
		std::string out;

		for (int i = 0; i < textLen; i++)
		{
			if (text[i] == 0)
				break;

			out.push_back((char)text[i]);
		}

		return TrimNullsAndSpaces(out);
	}

	// 1 = UTF-16 with BOM, 2 = UTF-16BE.
	// Minimal UTF-16 fallback: correctly handles ASCII-range characters.
	// This is enough for names like "Kazumi Totaka".
	bool bigEndian = encoding == 2;

	int pos = 0;

	if (encoding == 1 && textLen >= 2)
	{
		if (text[0] == 0xFE && text[1] == 0xFF)
		{
			bigEndian = true;
			pos = 2;
		}
		else if (text[0] == 0xFF && text[1] == 0xFE)
		{
			bigEndian = false;
			pos = 2;
		}
	}

	std::string out;

	for (; pos + 1 < textLen; pos += 2)
	{
		unsigned int ch = 0;

		if (bigEndian)
			ch = ((unsigned int)text[pos] << 8) | text[pos + 1];
		else
			ch = ((unsigned int)text[pos + 1] << 8) | text[pos];

		if (ch == 0)
			break;

		if (ch < 128)
			out.push_back((char)ch);
		else
			out.push_back('?');
	}

	return TrimNullsAndSpaces(out);
}

static void ParseID3ApicFrame(
	Game::RecordPlayerTrackInfo& info,
	const unsigned char* data,
	int len
)
{
	if (data == nullptr || len <= 5)
		return;

	int pos = 0;

	unsigned char encoding = data[pos++];

	std::string mime;

	while (pos < len && data[pos] != 0)
	{
		mime.push_back((char)data[pos]);
		pos++;
	}

	if (pos >= len)
		return;

	// Skip null after MIME.
	pos++;

	if (pos >= len)
		return;

	// Skip picture type.
	pos++;

	// Skip description.
	if (encoding == 1 || encoding == 2)
	{
		while (pos + 1 < len)
		{
			if (data[pos] == 0 && data[pos + 1] == 0)
			{
				pos += 2;
				break;
			}

			pos += 2;
		}
	}
	else
	{
		while (pos < len && data[pos] != 0)
			pos++;

		if (pos < len)
			pos++;
	}

	if (pos >= len)
		return;

	info.coverMime = mime;
	info.coverBytes.assign(data + pos, data + len);
	info.hasEmbeddedCover = !info.coverBytes.empty();
}

static void ApplyFilenameFallbackMetadata(
	Game::RecordPlayerTrackInfo& info,
	const std::string& path
)
{
	std::string base = StripPathAndExtensionForMusic(path);

	if (info.title == "Unknown Track" || info.title.empty())
	{
		info.title = base;
	}

	// If file is named "Artist - Title.mp3", use that as fallback.
	size_t split = base.find(" - ");

	if (split != std::string::npos)
	{
		if (info.artist == "Unknown Artist" || info.artist.empty())
			info.artist = base.substr(0, split);

		if (info.title == base || info.title == "Unknown Track" || info.title.empty())
			info.title = base.substr(split + 3);
	}
}


static Game::RecordPlayerTrackInfo ReadMp3Id3MetadataFromFile(const std::string& path)
{
	Game::RecordPlayerTrackInfo info{};

	ApplyFilenameFallbackMetadata(info, path);

	std::vector<unsigned char> bytes = ReadWholeBinaryFile(path);

	if (bytes.size() < 10)
		return info;

	if (bytes[0] != 'I' || bytes[1] != 'D' || bytes[2] != '3')
		return info;

	int majorVersion = bytes[3];
	unsigned char flags = bytes[5];

	int tagSize = ReadSynchsafe32(&bytes[6]);

	int pos = 10;
	int end = std::min((int)bytes.size(), pos + tagSize);

	// Skip extended header if present.
	if ((flags & 0x40) != 0 && pos + 4 < end)
	{
		if (majorVersion == 3)
		{
			int extSize = ReadBE32(&bytes[pos]);
			pos += 4 + extSize;
		}
		else if (majorVersion == 4)
		{
			int extSize = ReadSynchsafe32(&bytes[pos]);
			pos += extSize;
		}
	}

	while (pos + 10 <= end)
	{
		char frameIdChars[5] = {};
		frameIdChars[0] = (char)bytes[pos + 0];
		frameIdChars[1] = (char)bytes[pos + 1];
		frameIdChars[2] = (char)bytes[pos + 2];
		frameIdChars[3] = (char)bytes[pos + 3];

		std::string frameId = frameIdChars;

		if (frameId[0] == 0)
			break;

		int frameSize = 0;

		if (majorVersion == 4)
			frameSize = ReadSynchsafe32(&bytes[pos + 4]);
		else
			frameSize = ReadBE32(&bytes[pos + 4]);

		pos += 10;

		if (frameSize <= 0 || pos + frameSize > end)
			break;

		const unsigned char* payload = &bytes[pos];

		if (frameId == "TIT2")
		{
			std::string title = DecodeID3TextFrame(payload, frameSize);

			if (!title.empty())
				info.title = title;
		}
		else if (frameId == "TPE1")
		{
			std::string artist = DecodeID3TextFrame(payload, frameSize);

			if (!artist.empty())
				info.artist = artist;
		}
		else if (frameId == "TALB")
		{
			std::string album = DecodeID3TextFrame(payload, frameSize);

			if (!album.empty())
				info.album = album;
		}
		else if (frameId == "APIC")
		{
			ParseID3ApicFrame(info, payload, frameSize);
		}

		pos += frameSize;
	}

	ApplyFilenameFallbackMetadata(info, path);

	return info;
}

int Game::FindRecordPlayerTrackIndexByFilename(const std::string& wantedName) const
{
	std::string wanted = wantedName;
	std::transform(
		wanted.begin(),
		wanted.end(),
		wanted.begin(),
		[](unsigned char c) { return (char)std::tolower(c); }
	);

	for (int i = 0; i < (int)recordPlayerTrackPaths.size(); i++)
	{
		std::string path = recordPlayerTrackPaths[i];
		std::replace(path.begin(), path.end(), '\\', '/');

		size_t slash = path.find_last_of('/');
		std::string filename = slash == std::string::npos
			? path
			: path.substr(slash + 1);

		std::transform(
			filename.begin(),
			filename.end(),
			filename.begin(),
			[](unsigned char c) { return (char)std::tolower(c); }
		);

		if (filename == wanted)
			return i;
	}

	return -1;
}
void Game::LoadRecordPlayerTrack(int trackIndex)
{
	UnloadRecordPlayerTrack();

	if (recordPlayerTrackPaths.empty())
		return;

	if (trackIndex < 0)
		trackIndex = 0;

	trackIndex %= (int)recordPlayerTrackPaths.size();

	recordPlayerTrackIndex = trackIndex;

	const std::string& path = recordPlayerTrackPaths[recordPlayerTrackIndex];

	LoadRecordPlayerTrackDisplayInfo(recordPlayerTrackIndex);

	recordPlayerMusic = LoadMusicStream(path.c_str());
	recordPlayerMusic.looping = false;

	recordPlayerMusicLoaded = true;

	UpdateRecordPlayerMusicVolume();

	if (!recordPlayerPaused)
	{
		PlayMusicStream(recordPlayerMusic);
	}

	if (!recordPlayerPaused)
	{
		TriggerRecordPlayerTrackPopup();
	}

	TraceLog(LOG_INFO, "Loaded record player track: %s", path.c_str());
}

void Game::UpdateRecordPlayerTrackOverlay(float dt)
{
	if (!recordPlayerMusicLoaded)
	{
		recordPlayerTrackInfoAlpha = 0.0f;
		recordPlayerTrackInfoTimer = 0.0f;
		return;
	}

	if (recordPlayerTrackInfoTimer > 0.0f)
	{
		recordPlayerTrackInfoTimer -= dt;

		if (recordPlayerTrackInfoTimer < 0.0f)
			recordPlayerTrackInfoTimer = 0.0f;
	}

	bool shouldShow =
		recordPlayer.playerLookingAt ||
		(!recordPlayerPaused && recordPlayerTrackInfoTimer > 0.0f);

	float targetAlpha = shouldShow ? 1.0f : 0.0f;

	recordPlayerTrackInfoAlpha +=
		(targetAlpha - recordPlayerTrackInfoAlpha) *
		Clamp(recordPlayerTrackInfoFadeSpeed * dt, 0.0f, 1.0f);

	if (recordPlayerTrackInfoAlpha < 0.01f)
		recordPlayerTrackInfoAlpha = 0.0f;
}

void Game::ToggleRecordPlayerPlayPause()
{
	if (!recordPlayerMusicLoaded)
	{
		if (!recordPlayerTrackPaths.empty())
			LoadRecordPlayerTrack(recordPlayerTrackIndex);
		else
			return;
	}

	recordPlayerPaused = !recordPlayerPaused;

	if (recordPlayerPaused)
	{
		PauseMusicStream(recordPlayerMusic);
	}
	else
	{
		ResumeMusicStream(recordPlayerMusic);

		if (!IsMusicStreamPlaying(recordPlayerMusic))
		{
			PlayMusicStream(recordPlayerMusic);
		}
		TriggerRecordPlayerTrackPopup();
	}
}

Vector3 Game::GetRecordPlayerAudioPosition() const
{
	if (recordPlayer.rootIndex >= 0 &&
		recordPlayer.rootIndex < (int)sceneProps.size())
	{
		return sceneProps[recordPlayer.rootIndex].position;
	}

	return { 0.0f, 0.0f, 0.0f };
}

void Game::UpdateRecordPlayerMusicVolume()
{
	if (!recordPlayerMusicLoaded)
		return;

	Vector3 source = GetRecordPlayerAudioPosition();
	Vector3 listener = camera.position;

	float distance = Vector3Distance(listener, source);

	float t = 0.0f;

	if (recordPlayer.audioMaxDistance > recordPlayer.audioMinDistance)
	{
		t = (distance - recordPlayer.audioMinDistance) /
			(recordPlayer.audioMaxDistance - recordPlayer.audioMinDistance);
	}

	t = Clamp(t, 0.0f, 1.0f);

	// Smooth distance falloff.
	float attenuation = 1.0f - (t * t * (3.0f - 2.0f * t));

	// Important:
	// Master volume is already applied globally through SetMasterVolume(masterVolume).
	// Do not multiply masterVolume here or it gets applied twice.
	float finalVolume =
		musicVolume *
		recordPlayer.sourceVolume *
		attenuation;

	SetMusicVolume(recordPlayerMusic, Clamp(finalVolume, 0.0f, 1.0f));

	Vector3 forward = NormalizeSafe(Vector3Subtract(camera.target, camera.position));
	Vector3 right = NormalizeSafe(Vector3CrossProduct(forward, camera.up));
	Vector3 toSource = NormalizeSafe(Vector3Subtract(source, listener));

	float side = Vector3DotProduct(right, toSource);
	float pan = Clamp(0.5f + side * 0.5f, 0.0f, 1.0f);

	SetMusicPan(recordPlayerMusic, pan);
}

bool Game::IsLookingAtRecordPlayer() const
{
	if (!recordPlayer.initialized)
		return false;

	Ray ray{};
	ray.position = camera.position;
	ray.direction = NormalizeSafe(GetCameraForward());

	int indicesToTest[3] =
	{
		recordPlayer.rootIndex,
		recordPlayer.diskIndex,
		recordPlayer.handIndex
	};

	float closestDistance = FLT_MAX;
	bool hitAny = false;

	for (int idx : indicesToTest)
	{
		if (idx < 0 || idx >= (int)sceneProps.size())
			continue;

		const SceneProp& prop = sceneProps[idx];

		if (!prop.visible)
			continue;

		BoundingBox bounds{};

		if (!GetScenePropRenderBoundsWorld(prop, bounds))
			continue;

		RayCollision hit = GetRayCollisionBox(ray, bounds);

		if (!hit.hit)
			continue;

		if (hit.distance > recordPlayer.interactDistance)
			continue;

		if (hit.distance < closestDistance)
		{
			closestDistance = hit.distance;
			hitAny = true;
		}
	}

	return hitAny;
}

void Game::UpdateRecordPlayerInteraction()
{
	recordPlayer.playerLookingAt = IsLookingAtRecordPlayer();

	if (!recordPlayer.playerLookingAt)
		return;

	if (IsKeyPressed(KEY_E))
	{
		ToggleRecordPlayerPlayPause();
	}

	if (IsKeyPressed(KEY_Q))
	{
		SkipRecordPlayerTrack();
	}

	if (IsKeyPressed(KEY_R))
	{
		ToggleRecordPlayerPlaybackMode();
	}
}
void Game::ResetRecordPlayerVisualPose(bool pausePlayback)
{
	if (!recordPlayer.initialized)
		return;

	if (recordPlayer.diskIndex < 0 ||
		recordPlayer.diskIndex >= (int)sceneProps.size() ||
		recordPlayer.handIndex < 0 ||
		recordPlayer.handIndex >= (int)sceneProps.size())
	{
		return;
	}

	if (pausePlayback)
	{
		recordPlayerPaused = true;

		if (recordPlayerMusicLoaded)
		{
			PauseMusicStream(recordPlayerMusic);
		}
	}

	SceneProp& disk = sceneProps[recordPlayer.diskIndex];
	SceneProp& hand = sceneProps[recordPlayer.handIndex];

	// This is the important cleanup:
	// reset the AUTHORING/rest pose, not only the current animated pose.
	// Your selected RecordHand should return to Visual Rotation Offset 0,0,0.
	recordPlayer.baseDiskEditorRot = { 0.0f, 0.0f, 0.0f };
	recordPlayer.baseHandEditorRot = { 0.0f, 0.0f, 0.0f };

	recordPlayer.diskSpinDeg = 0.0f;
	recordPlayer.handT = recordPlayerPaused ? 0.0f : 1.0f;

	disk.importedEditorRotationDeg = recordPlayer.baseDiskEditorRot;
	hand.importedEditorRotationDeg = recordPlayer.baseHandEditorRot;

	ApplyRecordPlayerVisualPose();

	MarkShadowMapsDirty();

	TraceLog(
		LOG_INFO,
		"Reset record player visual pose. paused=%i handBase=(%.2f %.2f %.2f) handT=%.2f",
		recordPlayerPaused ? 1 : 0,
		recordPlayer.baseHandEditorRot.x,
		recordPlayer.baseHandEditorRot.y,
		recordPlayer.baseHandEditorRot.z,
		recordPlayer.handT
	);
}

bool Game::IsMainMenuMusicScreen(GameScreen screen) const
{
	return screen == GameScreen::MainMenu ||
		screen == GameScreen::Controls ||
		screen == GameScreen::AudioSettings ||
		screen == GameScreen::GraphicsSettings ||
		screen == GameScreen::Credits;
}

void Game::LoadMainMenuMusic()
{
	if (mainMenuMusicLoaded)
		return;

	if (!FileExists(mainMenuMusicPath.c_str()))
	{
		TraceLog(
			LOG_WARNING,
			"Main menu music missing: %s",
			mainMenuMusicPath.c_str()
		);

		return;
	}

	mainMenuMusic = LoadMusicStream(mainMenuMusicPath.c_str());
	mainMenuMusic.looping = true;

	mainMenuMusicLoaded = true;
	mainMenuMusicPlaying = false;

	UpdateMainMenuMusicVolume();

	TraceLog(
		LOG_INFO,
		"Loaded main menu music: %s",
		mainMenuMusicPath.c_str()
	);
}

void Game::UnloadMainMenuMusic()
{
	if (!mainMenuMusicLoaded)
		return;

	StopMusicStream(mainMenuMusic);
	UnloadMusicStream(mainMenuMusic);

	mainMenuMusic = {};
	mainMenuMusicLoaded = false;
	mainMenuMusicPlaying = false;
}

void Game::StartMainMenuMusic()
{
	if (!mainMenuMusicLoaded)
	{
		LoadMainMenuMusic();
	}

	if (!mainMenuMusicLoaded)
		return;

	UpdateMainMenuMusicVolume();

	if (!mainMenuMusicPlaying || !IsMusicStreamPlaying(mainMenuMusic))
	{
		PlayMusicStream(mainMenuMusic);
		mainMenuMusicPlaying = true;
	}
}

void Game::StopMainMenuMusic()
{
	if (!mainMenuMusicLoaded)
		return;

	if (IsMusicStreamPlaying(mainMenuMusic))
	{
		StopMusicStream(mainMenuMusic);
	}

	mainMenuMusicPlaying = false;
}

void Game::UpdateMainMenuMusic(float dt)
{
	(void)dt;

	if (!mainMenuMusicLoaded)
		return;

	if (!mainMenuMusicPlaying)
		return;

	UpdateMusicStream(mainMenuMusic);
	UpdateMainMenuMusicVolume();
}

void Game::UpdateMainMenuMusicVolume()
{
	if (!mainMenuMusicLoaded)
		return;

	float volume = masterVolume * musicVolume * mainMenuMusicVolume;
	volume = Clamp(volume, 0.0f, 1.0f);

	SetMusicVolume(mainMenuMusic, volume);
}

void Game::ApplyRecordPlayerVisualPose()
{
	if (!recordPlayer.initialized)
		return;

	if (recordPlayer.diskIndex < 0 ||
		recordPlayer.diskIndex >= (int)sceneProps.size() ||
		recordPlayer.handIndex < 0 ||
		recordPlayer.handIndex >= (int)sceneProps.size())
	{
		return;
	}

	SceneProp& disk = sceneProps[recordPlayer.diskIndex];
	SceneProp& hand = sceneProps[recordPlayer.handIndex];

	disk.importedEditorRotationDeg = recordPlayer.baseDiskEditorRot;
	disk.importedEditorRotationDeg.y += recordPlayer.diskSpinDeg;

	hand.importedEditorRotationDeg = recordPlayer.baseHandEditorRot;
	hand.importedEditorRotationDeg.y +=
		recordPlayer.handPlayYawDeg * recordPlayer.handT;

	ApplyImportedEditorTransformToRuntime(disk);
	ApplyImportedEditorTransformToRuntime(hand);
}

void Game::UpdateRecordPlayerVisuals(float dt)
{
	if (!recordPlayer.initialized)
		return;

	if (recordPlayer.diskIndex < 0 ||
		recordPlayer.diskIndex >= (int)sceneProps.size() ||
		recordPlayer.handIndex < 0 ||
		recordPlayer.handIndex >= (int)sceneProps.size())
	{
		return;
	}

	SceneProp& disk = sceneProps[recordPlayer.diskIndex];
	SceneProp& hand = sceneProps[recordPlayer.handIndex];

	float targetHandT = recordPlayerPaused ? 0.0f : 1.0f;

	recordPlayer.handT +=
		(targetHandT - recordPlayer.handT) *
		Clamp(recordPlayer.handAnimSpeed * dt, 0.0f, 1.0f);

	if (!recordPlayerPaused)
	{
		recordPlayer.diskSpinDeg += recordPlayer.diskSpinSpeedDeg * dt;

		while (recordPlayer.diskSpinDeg >= 360.0f)
			recordPlayer.diskSpinDeg -= 360.0f;
	}

	// Blender Z-up becomes Raylib Y-up for this imported GLB scene.
	// If the disk spins on the wrong axis, move this value to .z instead.
	ApplyRecordPlayerVisualPose();
}

void Game::UpdateRecordPlayerMusicStream()
{
	if (!recordPlayerMusicLoaded)
		return;

	UpdateMusicStream(recordPlayerMusic);
	UpdateRecordPlayerMusicVolume();

	if (recordPlayerPaused)
		return;

	float played = GetMusicTimePlayed(recordPlayerMusic);
	float length = GetMusicTimeLength(recordPlayerMusic);

	if (length > 0.1f && played >= length - 0.05f)
	{
		AdvanceRecordPlayerTrack(false);
	}
}

void Game::UpdateRecordPlayer(float dt)
{
	UpdateRecordPlayerMusicStream();

	if (currentScreen == GameScreen::Playing)
	{
		UpdateRecordPlayerInteraction();
		UpdateRecordPlayerVisuals(dt);
	}

	UpdateRecordPlayerTrackOverlay(dt);
}

void Game::DrawRecordPlayerTrackOverlay() const
{
	if (recordPlayerTrackInfoAlpha <= 0.01f)
		return;

	if (!recordPlayerMusicLoaded)
		return;

	float alpha = Clamp(recordPlayerTrackInfoAlpha, 0.0f, 1.0f);

	unsigned char bgA = (unsigned char)(190.0f * alpha);
	unsigned char textA = (unsigned char)(255.0f * alpha);
	unsigned char subTextA = (unsigned char)(190.0f * alpha);

	Font font = uiFontLoaded ? uiFont : GetFontDefault();

	float x = 24.0f;

	const float storeHudY = showPerfOverlay ? 340.0f : 24.0f;
	const float storeHudH = storeDayState == StoreDayState::Closing
		? 118.0f
		: 88.0f;

	const float panelGap = 8.0f;

	float y = storeHudY + storeHudH + panelGap;

	Rectangle bg{
		x,
		y,
		420.0f,
		104.0f
	};

	DrawRectangleRounded(
		bg,
		0.18f,
		10,
		Color{ 0, 0, 0, bgA }
	);

	DrawRectangleRoundedLines(
		bg,
		0.18f,
		10,
		Color{ 255, 255, 255, (unsigned char)(55.0f * alpha) }
	);

	Rectangle coverRect{
		x + 14.0f,
		y + 14.0f,
		76.0f,
		76.0f
	};

	if (recordPlayerCoverLoaded)
	{
		Rectangle src{
			0.0f,
			0.0f,
			(float)recordPlayerCoverTexture.width,
			(float)recordPlayerCoverTexture.height
		};

		DrawTexturePro(
			recordPlayerCoverTexture,
			src,
			coverRect,
			{ 0.0f, 0.0f },
			0.0f,
			Color{ 255, 255, 255, textA }
		);
	}
	else
	{
		DrawRectangleRounded(
			coverRect,
			0.18f,
			8,
			Color{ 35, 38, 45, (unsigned char)(220.0f * alpha) }
		);

		DrawCircle(
			(int)(coverRect.x + coverRect.width * 0.5f),
			(int)(coverRect.y + coverRect.height * 0.5f),
			26.0f,
			Color{ 90, 95, 105, textA }
		);

		DrawCircle(
			(int)(coverRect.x + coverRect.width * 0.5f),
			(int)(coverRect.y + coverRect.height * 0.5f),
			6.0f,
			Color{ 15, 15, 18, textA }
		);
	}

	float textX = coverRect.x + coverRect.width + 16.0f;
	float textY = y + 18.0f;

	DrawTextEx(
		font,
		recordPlayerCurrentTitle.c_str(),
		{ textX, textY },
		22.0f,
		1.0f,
		Color{ 255, 255, 255, textA }
	);

	DrawTextEx(
		font,
		recordPlayerCurrentArtist.c_str(),
		{ textX, textY + 30.0f },
		18.0f,
		1.0f,
		Color{ 190, 200, 215, subTextA }
	);

	const char* thirdLine = recordPlayerCurrentAlbum.empty()
		? TextFormat("Mode: %s", GetRecordPlayerPlaybackModeName())
		: recordPlayerCurrentAlbum.c_str();

	DrawTextEx(
		font,
		thirdLine,
		{ textX, textY + 56.0f },
		16.0f,
		1.0f,
		Color{ 150, 165, 190, subTextA }
	);
}

const char* Game::GetRecordPlayerPlaybackModeName() const
{
	switch (recordPlayerPlaybackMode)
	{
	case RecordPlayerPlaybackMode::Sequential:
		return "Sequential";

	case RecordPlayerPlaybackMode::LoopCurrent:
		return "Loop Current";

	case RecordPlayerPlaybackMode::Shuffle:
		return "Shuffle";

	default:
		return "Unknown";
	}
}

void Game::ToggleRecordPlayerPlaybackMode()
{
	switch (recordPlayerPlaybackMode)
	{
	case RecordPlayerPlaybackMode::Sequential:
		recordPlayerPlaybackMode = RecordPlayerPlaybackMode::LoopCurrent;
		break;

	case RecordPlayerPlaybackMode::LoopCurrent:
		recordPlayerPlaybackMode = RecordPlayerPlaybackMode::Shuffle;
		break;

	case RecordPlayerPlaybackMode::Shuffle:
	default:
		recordPlayerPlaybackMode = RecordPlayerPlaybackMode::Sequential;
		break;
	}

	TraceLog(
		LOG_INFO,
		"Record player playback mode: %s",
		GetRecordPlayerPlaybackModeName()
	);
}

int Game::GetNextRecordPlayerTrackIndex(bool manualSkip) const
{
	int count = (int)recordPlayerTrackPaths.size();

	if (count <= 0)
		return -1;

	if (count == 1)
		return 0;

	// Auto-end behavior:
	// LoopCurrent only loops when the song naturally finishes.
	// Manual Q skip should still move to another track.
	if (!manualSkip &&
		recordPlayerPlaybackMode == RecordPlayerPlaybackMode::LoopCurrent)
	{
		return recordPlayerTrackIndex;
	}

	if (recordPlayerPlaybackMode == RecordPlayerPlaybackMode::Shuffle)
	{
		int nextIndex = recordPlayerTrackIndex;

		for (int attempt = 0; attempt < 8 && nextIndex == recordPlayerTrackIndex; attempt++)
		{
			nextIndex = GetRandomValue(0, count - 1);
		}

		// Fallback so shuffle does not repeat the same song if random keeps choosing it.
		if (nextIndex == recordPlayerTrackIndex)
		{
			nextIndex = (recordPlayerTrackIndex + 1) % count;
		}

		return nextIndex;
	}

	// Sequential mode.
	return (recordPlayerTrackIndex + 1) % count;
}

void Game::AdvanceRecordPlayerTrack(bool manualSkip)
{
	if (recordPlayerTrackPaths.empty())
		return;

	int nextIndex = GetNextRecordPlayerTrackIndex(manualSkip);

	if (nextIndex < 0)
		return;

	bool wasPaused = recordPlayerPaused;

	// Loop current song without unloading/reloading the stream.
	if (!manualSkip &&
		recordPlayerPlaybackMode == RecordPlayerPlaybackMode::LoopCurrent &&
		nextIndex == recordPlayerTrackIndex &&
		recordPlayerMusicLoaded)
	{
		SeekMusicStream(recordPlayerMusic, 0.0f);

		if (!recordPlayerPaused && !IsMusicStreamPlaying(recordPlayerMusic))
		{
			PlayMusicStream(recordPlayerMusic);
		}

		return;
	}

	recordPlayerPaused = false;
	LoadRecordPlayerTrack(nextIndex);

	if (wasPaused)
	{
		PauseMusicStream(recordPlayerMusic);
		recordPlayerPaused = true;
	}
}

void Game::SkipRecordPlayerTrack()
{
	AdvanceRecordPlayerTrack(true);
}



static std::string StripPathAndExtension(const std::string& path)
{
	std::string p = path;

	std::replace(p.begin(), p.end(), '\\', '/');

	size_t slash = p.find_last_of('/');
	size_t start = slash == std::string::npos ? 0 : slash + 1;

	size_t dot = p.find_last_of('.');

	if (dot == std::string::npos || dot < start)
		dot = p.size();

	return p.substr(start, dot - start);
}

static std::vector<std::string> SplitByPipe(const std::string& line)
{
	std::vector<std::string> result;

	size_t start = 0;

	while (start <= line.size())
	{
		size_t pipe = line.find('|', start);

		if (pipe == std::string::npos)
		{
			result.push_back(TrimString(line.substr(start)));
			break;
		}

		result.push_back(TrimString(line.substr(start, pipe - start)));
		start = pipe + 1;
	}

	return result;
}

bool Game::IsRecordPlayerAnimatedPartIndex(int scenePropIndex) const
{
	return recordPlayer.initialized &&
		(scenePropIndex == recordPlayer.diskIndex ||
			scenePropIndex == recordPlayer.handIndex);
}

void Game::LoadRecordPlayerPlaylist()
{
	recordPlayerTrackPaths.clear();
	recordPlayerTrackInfos.clear();

	std::ifstream in("Audio/RecordPlayer/playlist.txt");

	if (!in.is_open())
	{
		TraceLog(LOG_WARNING, "No Audio/RecordPlayer/playlist.txt found.");
		return;
	}

	std::string line;

	while (std::getline(in, line))
	{
		line = TrimString(line);

		if (line.empty())
			continue;

		if (line[0] == '#')
			continue;

		recordPlayerTrackPaths.push_back(line);
		recordPlayerTrackInfos.push_back(ReadMp3Id3MetadataFromFile(line));
	}

	if (recordPlayerTrackPaths.empty())
	{
		TraceLog(LOG_WARNING, "Record player playlist is empty.");
		return;
	}

	int shopChannelIndex =
		FindRecordPlayerTrackIndexByFilename("Kazumi Totaka - Shop Channel.mp3");

	if (shopChannelIndex < 0)
	{
		shopChannelIndex =
			FindRecordPlayerTrackIndexByFilename("Shop Channel.mp3");
	}

	recordPlayerTrackIndex = shopChannelIndex >= 0 ? shopChannelIndex : 0;

	// Important: set paused false BEFORE loading.
	// LoadRecordPlayerTrack() only plays immediately if recordPlayerPaused == false.
	recordPlayerPaused = false;

	LoadRecordPlayerTrack(recordPlayerTrackIndex);

	if (recordPlayerMusicLoaded)
	{
		PlayMusicStream(recordPlayerMusic);
		TriggerRecordPlayerTrackPopup();
	}
}

void Game::UnloadRecordPlayerCoverImage()
{
	if (recordPlayerCoverLoaded)
	{
		UnloadTexture(recordPlayerCoverTexture);
		recordPlayerCoverTexture = {};
		recordPlayerCoverLoaded = false;
	}
}

void Game::LoadRecordPlayerTrackDisplayInfo(int trackIndex)
{
	UnloadRecordPlayerCoverImage();

	recordPlayerCurrentArtist = "Unknown Artist";
	recordPlayerCurrentTitle = "Unknown Track";
	recordPlayerCurrentAlbum = "";
	recordPlayerCurrentCoverPath.clear();

	if (trackIndex >= 0 && trackIndex < (int)recordPlayerTrackInfos.size())
	{
		const RecordPlayerTrackInfo& info = recordPlayerTrackInfos[trackIndex];

		recordPlayerCurrentArtist = info.artist;
		recordPlayerCurrentTitle = info.title;
		recordPlayerCurrentAlbum = info.album;
		recordPlayerCurrentCoverPath = info.coverImagePath;

		if (info.hasEmbeddedCover && !info.coverBytes.empty())
		{
			const char* ext = ".jpg";

			std::string mime = info.coverMime;
			std::transform(
				mime.begin(),
				mime.end(),
				mime.begin(),
				[](unsigned char c) { return (char)std::tolower(c); }
			);

			if (mime.find("png") != std::string::npos)
				ext = ".png";
			else if (mime.find("jpeg") != std::string::npos || mime.find("jpg") != std::string::npos)
				ext = ".jpg";

			Image img = LoadImageFromMemory(
				ext,
				info.coverBytes.data(),
				(int)info.coverBytes.size()
			);

			if (img.data != nullptr)
			{
				recordPlayerCoverTexture = LoadTextureFromImage(img);
				UnloadImage(img);

				if (recordPlayerCoverTexture.id != 0)
				{
					recordPlayerCoverLoaded = true;
					SetTextureFilter(recordPlayerCoverTexture, TEXTURE_FILTER_BILINEAR);
					return;
				}
			}
		}

		// Optional fallback: sidecar image if embedded art is missing.
		if (!info.coverImagePath.empty() && FileExists(info.coverImagePath.c_str()))
		{
			recordPlayerCoverTexture = LoadTexture(info.coverImagePath.c_str());

			if (recordPlayerCoverTexture.id != 0)
			{
				recordPlayerCoverLoaded = true;
				SetTextureFilter(recordPlayerCoverTexture, TEXTURE_FILTER_BILINEAR);
			}
		}

		return;
	}

	if (trackIndex >= 0 && trackIndex < (int)recordPlayerTrackPaths.size())
	{
		recordPlayerCurrentTitle =
			StripPathAndExtensionForMusic(recordPlayerTrackPaths[trackIndex]);
	}
}

void Game::TriggerRecordPlayerTrackPopup()
{
	recordPlayerTrackInfoTimer = recordPlayerTrackInfoPopupSeconds;
}

Game::RecordPlayerTrackInfo Game::ParseRecordPlayerTrackInfo(
	const std::string& line,
	std::string& outAudioPath
) const
{
	RecordPlayerTrackInfo info{};

	std::vector<std::string> parts = SplitByPipe(line);

	if (parts.empty())
	{
		outAudioPath.clear();
		return info;
	}

	outAudioPath = parts[0];

	if (parts.size() >= 2 && !parts[1].empty())
		info.artist = parts[1];

	if (parts.size() >= 3 && !parts[2].empty())
		info.title = parts[2];
	else
		info.title = StripPathAndExtension(outAudioPath);

	if (parts.size() >= 4 && !parts[3].empty())
		info.coverImagePath = parts[3];
	else
	{
		// Optional auto-detect beside the MP3.
		std::string base = outAudioPath;
		size_t dot = base.find_last_of('.');

		if (dot != std::string::npos)
			base = base.substr(0, dot);

		std::string png = base + ".png";
		std::string jpg = base + ".jpg";

		if (FileExists(png.c_str()))
			info.coverImagePath = png;
		else if (FileExists(jpg.c_str()))
			info.coverImagePath = jpg;
	}

	return info;
}

void Game::InitializeRecordPlayerController()
{
	recordPlayer.rootIndex = FindScenePropByNodeNameExact("RecordPlayer");
	recordPlayer.diskIndex = FindScenePropByNodeNameExact("RecordDisk");
	recordPlayer.handIndex = FindScenePropByNodeNameExact("RecordHand");

	recordPlayer.initialized =
		recordPlayer.rootIndex >= 0 &&
		recordPlayer.diskIndex >= 0 &&
		recordPlayer.handIndex >= 0;

	if (!recordPlayer.initialized)
	{
		TraceLog(
			LOG_WARNING,
			"Record player nodes not found. root=%i disk=%i hand=%i",
			recordPlayer.rootIndex,
			recordPlayer.diskIndex,
			recordPlayer.handIndex
		);
		return;
	}

	SceneProp& root = sceneProps[recordPlayer.rootIndex];
	SceneProp& disk = sceneProps[recordPlayer.diskIndex];
	SceneProp& hand = sceneProps[recordPlayer.handIndex];

	// The music player is interactable but should not be picked up.
	root.canPickup = false;
	root.hasCollision = false;
	root.blocksPlayer = false;

	disk.canPickup = false;
	hand.canPickup = false;

	recordPlayer.baseDiskEditorRot = disk.importedEditorRotationDeg;
	recordPlayer.baseHandEditorRot = hand.importedEditorRotationDeg;

	TraceLog(
		LOG_INFO,
		"Record player initialized: root=%i disk=%i hand=%i",
		recordPlayer.rootIndex,
		recordPlayer.diskIndex,
		recordPlayer.handIndex
	);

	TraceLog(
		LOG_INFO,
		"RecordPlayer lookup: root=%i disk=%i hand=%i",
		recordPlayer.rootIndex,
		recordPlayer.diskIndex,
		recordPlayer.handIndex
	);
}

float Game::GetVoiceVolume() const
{
	return Clamp(masterVolume * soundFxVolume * voiceVolume, 0.0f, 1.0f);
}

std::string Game::VoicePath(
	const std::string& scriptId,
	int nodeIndex
) const
{
	return TextFormat(
		"Audio/Voices/%s_n%02i.mp3",
		scriptId.c_str(),
		nodeIndex
	);
}

std::string Game::SharedVoicePath(
	const std::string& fileBaseName
) const
{
	return TextFormat(
		"Audio/Voices/%s.mp3",
		fileBaseName.c_str()
	);

}

bool Game::PlayDialogueVoiceIfAvailable(const std::string& voicePath)
{
	if (hasDialogueVoice)
	{
		StopSound(currentDialogueVoice);
		UnloadSound(currentDialogueVoice);
		hasDialogueVoice = false;
	}

	if (voicePath.empty())
		return false;

	if (!FileExists(voicePath.c_str()))
	{
		TraceLog(
			LOG_WARNING,
			"Dialogue voice file missing, skipping: %s",
			voicePath.c_str()
		);

		return false;
	}

	currentDialogueVoice = LoadSound(voicePath.c_str());

	if (currentDialogueVoice.frameCount == 0)
	{
		TraceLog(
			LOG_WARNING,
			"Dialogue voice failed to load, skipping: %s",
			voicePath.c_str()
		);

		return false;
	}

	SetSoundVolume(currentDialogueVoice, GetVoiceVolume());
	PlaySound(currentDialogueVoice);

	hasDialogueVoice = true;
	return true;
}
void Game::StartDialogueFallbackBeepIfNeeded()
{
	if (currentDialogueHasValidVoice)
		return;

	if (dialogueTextComplete)
		return;

	if (dialogueFallbackBeepActive)
		return;

	PlayLoopingSfx("dialogue_beep", true);
	dialogueFallbackBeepActive = true;
}

void Game::StopDialogueFallbackBeep()
{
	if (!dialogueFallbackBeepActive)
		return;

	StopLoopingSfx("dialogue_beep");
	dialogueFallbackBeepActive = false;
}

void Game::UpdateDialogueFallbackBeep()
{
	if (!dialogueActive)
	{
		StopDialogueFallbackBeep();
		return;
	}

	if (currentDialogueHasValidVoice)
	{
		StopDialogueFallbackBeep();
		return;
	}

	if (dialogueTextComplete)
	{
		StopDialogueFallbackBeep();
		return;
	}

	StartDialogueFallbackBeepIfNeeded();

}


Game::DialogueScript* Game::GetActiveDialogueScript()
{
	auto it = dialogueScripts.find(activeDialogueScriptId);

	if (it == dialogueScripts.end())
	{
		return nullptr;
	}

	return &it->second;
}

Game::DialogueNode* Game::GetActiveDialogueNode()
{
	DialogueScript* script = GetActiveDialogueScript();

	if (script == nullptr)
	{
		return nullptr;
	}

	if (currentDialogueNode < 0 || currentDialogueNode >= (int)script->nodes.size())
	{
		return nullptr;
	}

	return &script->nodes[currentDialogueNode];
}

void Game::UpdateDialogue()
{
	if (customerDialogueManager == nullptr) return;

	if (!dialogueActive)
	{
		int talkIndex = FindTalkableCustomer();

		if (talkIndex >= 0 && IsKeyPressed(KEY_E) && !hasHeldBody && !inspectMode && !editMode)
		{
			StartCustomerDialogue(talkIndex);
		}

		return;
	}

	UpdateDialogueManager(customerDialogueManager);
	UpdateDialogueTextAnimation();

	UpdateDialogueFallbackBeep();

	DialogueNode* currentNode = GetActiveDialogueNode();

	if (currentNode == nullptr)
	{
		CloseDialogue();
		return;
	}

	if (IsCurrentDialogueCustomerDialogue() && IsKeyPressed(KEY_F))
	{
		customerDialogueAutoPlayEnabled = !customerDialogueAutoPlayEnabled;
		currentDialogueAutoPlayEnabled = customerDialogueAutoPlayEnabled;

		lastTransactionDeltaYen = 0;
		lastTransactionText = customerDialogueAutoPlayEnabled
			? "Customer dialogue autoplay: ON"
			: "Customer dialogue autoplay: OFF";
	}

	if (!currentNode->choices.empty())
	{
		int choiceCount = (int)currentNode->choices.size();

		if (IsKeyPressed(KEY_UP))
		{
			selectedDialogueChoice =
				(selectedDialogueChoice + choiceCount - 1) % choiceCount;
		}

		if (IsKeyPressed(KEY_DOWN))
		{
			selectedDialogueChoice =
				(selectedDialogueChoice + 1) % choiceCount;
		}
	}

	if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER))
	{
		dialogueAutoAdvanceTimer = 0.0f;

		if (!dialogueTextComplete)
		{
			CompleteDialogueTextAnimation();
			return;
		}

		AdvanceDialogueFromCurrentNode();
		return;
	}


	if (IsKeyPressed(KEY_LEFT_ALT))
	{
		CloseDialogue();
		return;
	}

	UpdateDialogueAutoPlay(GetFrameTime());
}
bool Game::IsSelfInspectDialogueActive() const
{
	return dialogueActive &&
		activeDialogueScriptId == "__self_inspect";
}




void Game::StartSelfDialogueLines(
	const std::vector<SelfDialogueLine>& lines,
	const std::string& scriptId
)
{
	if (lines.empty())
		return;

	if (dialogueActive)
	{
		CloseDialogue();
	}

	DialogueScript script{};
	script.id = scriptId;

	for (int i = 0; i < (int)lines.size(); i++)
	{
		DialogueNode node{};
		node.speaker = "Player";
		node.text = lines[i].text;

		node.nextNode = i + 1 < (int)lines.size()
			? i + 1
			: -1;

		node.endDialogue = i + 1 >= (int)lines.size();

		node.applyCustomerAnim = false;
		node.customerAnim = CustomerAnimState::Idle;

		node.voicePath = lines[i].voicePath;
		node.sfxId = lines[i].sfxId;

		node.nextScriptOnEnd = "";
		node.action = DialogueAction::None;

		script.nodes.push_back(node);
	}

	dialogueScripts[script.id] = script;

	dialogueActive = true;
	dialogueCustomerIndex = -1;
	activeDialogueCustomerIndex = -1;
	activeDialogueScriptId = script.id;

	currentDialogueNode = 0;
	selectedDialogueChoice = 0;

	SetDialogueNode(currentDialogueNode);
}
void Game::StartSelfDialogueLines(
	const std::vector<std::string>& lines,
	const std::string& scriptId
)
{
	std::vector<SelfDialogueLine> selfLines;
	selfLines.reserve(lines.size());

	std::string voicePrefix = scriptId;

	if (voicePrefix.rfind("__", 0) == 0)
	{
		voicePrefix.erase(0, 2);
	}

	for (int i = 0; i < (int)lines.size(); i++)
	{
		SelfDialogueLine line{};
		line.text = lines[i];
		line.voicePath = MakeVoicePathFromPrefix(voicePrefix, i);
		line.sfxId = "";

		selfLines.push_back(line);
	}

	StartSelfDialogueLines(selfLines, scriptId);
}


void Game::SetDialogueLabelText(const std::string& text)
{
	if (customerDialogueLabel == nullptr) return;

	visibleDialogueText = text;

	RayDialLabelData* labelData = (RayDialLabelData*)customerDialogueLabel->data;
	labelData->text = visibleDialogueText.c_str();
}
void Game::StartDialogueTextAnimation(const std::string& text, float charsPerSecond)
{
	fullDialogueText = text;
	visibleDialogueText.clear();

	dialogueCharsPerSecond = charsPerSecond;
	dialogueTextStartTime = GetTime();
	dialogueTextComplete = false;

	SetDialogueLabelText("");
}
void Game::CompleteDialogueTextAnimation()
{
	dialogueTextComplete = true;
	SetDialogueLabelText(fullDialogueText);

	StopDialogueFallbackBeep();
}

std::string Game::JoinLines(const std::vector<std::string>& lines)
{
	std::string result;

	for (int i = 0; i < (int)lines.size(); i++)
	{
		if (i > 0)
			result += "\n";

		result += lines[i];
	}

	return result;
}

std::vector<std::string> Game::SplitLines(const std::string& text)
{
	std::vector<std::string> lines;

	size_t start = 0;

	while (start <= text.size())
	{
		size_t end = text.find('\n', start);

		std::string line = end == std::string::npos
			? text.substr(start)
			: text.substr(start, end - start);

		line = TrimString(line);

		if (!line.empty())
			lines.push_back(line);

		if (end == std::string::npos)
			break;

		start = end + 1;
	}

	return lines;
}

void Game::DrawBottomPrompt(const char* text, int yOffset) const
{
	if (text == nullptr || text[0] == '\0')
		return;

	int fontSize = 24;
	int textWidth = MeasureText(text, fontSize);

	int x = GetScreenWidth() / 2 - textWidth / 2;
	int y = GetScreenHeight() - yOffset;

	Rectangle bg{
		(float)x - 18.0f,
		(float)y - 10.0f,
		(float)textWidth + 36.0f,
		(float)fontSize + 20.0f
	};

	DrawRectangleRounded(
		bg,
		0.35f,
		8,
		Color{ 0, 0, 0, 150 }
	);

	DrawRectangleRoundedLines(
		bg,
		0.35f,
		8,
		Color{ 255, 255, 255, 90 }
	);

	DrawText(text, x, y, fontSize, WHITE);
}

void Game::DrawGameplayPrompts()
{
	blockedInteractionPrompt.clear();
	if (editMode || cursorUnlocked)
		return;


	if (targetedScenePropIndex >= 0 &&
		targetedScenePropIndex < (int)sceneProps.size())
	{
		const SceneProp& prop = sceneProps[targetedScenePropIndex];

		if (IsStoreOpenControlProp(prop))
		{
			if (storeDayState == StoreDayState::Closed)
			{
				DrawBottomPrompt("Press E to open store", 120);
			}
			else if (storeDayState == StoreDayState::Open)
			{
				DrawBottomPrompt("Store is already open", 120);
			}
			else if (storeDayState == StoreDayState::Closing)
			{
				DrawBottomPrompt("Store is closing", 120);
			}
			else
			{
				DrawBottomPrompt("Day complete", 120);
			}

			return;
		}

		if (IsStoreCloseControlProp(prop))
		{
			if (storeDayState == StoreDayState::Open)
			{
				DrawBottomPrompt("Press E to close store early", 120);
			}
			else if (storeDayState == StoreDayState::Closing)
			{
				DrawBottomPrompt("Closing: waiting for customers to leave", 120);
			}
			else if (storeDayState == StoreDayState::Closed)
			{
				DrawBottomPrompt("Store is already closed", 120);
			}
			else
			{
				DrawBottomPrompt("Day complete", 120);
			}

			return;
		}
	}

	if (inspectMode)
	{
		DrawBottomPrompt(
			"Press F to stop inspecting   |  Left Mouse drag to rotate   |   Mouse wheel to zoom",
			58
		);

		return;
	}

	if (recordPlayer.playerLookingAt)
	{
		const char* action = recordPlayerPaused ? "play" : "pause";

		DrawBottomPrompt(
			TextFormat(
				"Press E to %s record | Q next track | R mode: %s",
				action,
				GetRecordPlayerPlaybackModeName()
			),
			120
		);

		return;
	}
	if (hasHeldBody)
	{
		if (heldItemScanState != HeldItemScanState::None)
		{
			DrawBottomPrompt("Scanning...", 118);
			return;
		}



		if (placementPreviewValid && targetedPlacementSpotIndex >= 0)
		{
			if (IsScannerSpot(targetedPlacementSpotIndex))
			{
				DrawBottomPrompt("Press E to scan", 118);
			}
			else
			{
				DrawBottomPrompt("Press E to place", 118);
			}
		}
		else
		{
			DrawBottomPrompt("Press E to drop", 118);
		}

		DrawBottomPrompt("Press F to inspect", 72);
		return;
	}


	if (targetedScenePropIndex >= 0 &&
		targetedScenePropIndex < (int)sceneProps.size())
	{
		const SceneProp& prop = sceneProps[targetedScenePropIndex];

		if (prop.canPickup)
		{
			DrawBottomPrompt("Press E to pick up", 120);
			return;
		}
	}

	int talkIndex = FindTalkableCustomer();

	if (!blockedInteractionPrompt.empty())
	{
		DrawBottomPrompt(blockedInteractionPrompt.c_str(), 120);
		return;
	}

	if (talkIndex >= 0)
	{
		DrawBottomPrompt("Press E to talk", 120);
		return;
	}
}

void Game::DrawStoreStatusHUD() const
{
	if (currentScreen != GameScreen::Playing)
		return;

	const char* stateText = "Closed";

	switch (storeDayState)
	{
	case StoreDayState::Closed:
		stateText = "Closed";
		break;

	case StoreDayState::Open:
		stateText = "Open";
		break;

	case StoreDayState::Closing:
		stateText = "Closing";
		break;

	case StoreDayState::Results:
		stateText = "Results";
		break;
	}

	Font font = uiFontLoaded ? uiFont : GetFontDefault();

	float x = 24.0f;
	float y = showPerfOverlay ? 340.0f : 24.0f;

	bool showClosingMessage = storeDayState == StoreDayState::Closing;

	StoreDayConfig cfg = GetCurrentStoreDayConfig();

	const float hudW = 420.0f;

	Rectangle bg{
		x,
		y,
		hudW,
		showClosingMessage ? 118.0f : 88.0f
	};

	DrawRectangleRounded(
		bg,
		0.18f,
		10,
		Color{ 0, 0, 0, 190 }
	);

	DrawRectangleRoundedLines(
		bg,
		0.18f,
		10,
		Color{ 255, 255, 255, 55 }
	);

	// Row 1: Day + store state
	std::string dayText = TextFormat(
		"Day %i/%i - Store: %s",
		currentStoreDay,
		maxStoreDays,
		stateText
	);

	DrawTextEx(
		font,
		dayText.c_str(),
		Vector2{ x + 16.0f, y + 14.0f },
		22.0f,
		1.0f,
		WHITE
	);

	// Row 2: Customer progress
	std::string customerText = TextFormat(
		"Customers: %i/%i    Done: %i",
		customersSpawnedToday,
		cfg.customerTarget,
		customersCompletedToday
	);

	DrawTextEx(
		font,
		customerText.c_str(),
		Vector2{ x + 16.0f, y + 44.0f },
		18.0f,
		1.0f,
		Color{ 190, 200, 215, 255 }
	);

	// Row 3: Quotas
	std::string quotaText = TextFormat(
		"Sales: \xC2\xA5%i/\xC2\xA5%i  Buys: \xC2\xA5%i/\xC2\xA5%i",
		buyerSalesToday,
		cfg.minBuyerSalesYen,
		sellerBuySpendToday,
		cfg.minSellerBuyYen
	);

	DrawTextEx(
		font,
		quotaText.c_str(),
		Vector2{ x + 16.0f, y + 68.0f },
		15.0f,
		1.0f,
		Color{ 150, 165, 190, 255 }
	);

	if (showClosingMessage)
	{
		DrawTextEx(
			font,
			"No more customers. Waiting for remaining customers to leave.",
			Vector2{ x + 16.0f, y + 96.0f },
			16.0f,
			1.0f,
			Color{ 210, 210, 210, 255 }
		);
	}
}

void Game::RunDialogueAction(const DialogueNode& node)
{
	if (activeDialogueCustomerIndex < 0 ||
		activeDialogueCustomerIndex >= (int)customers.size())
	{
		return;
	}

	switch (node.action)
	{
	case DialogueAction::PlaceCounterItem:
		EnsureCustomerCounterItemPlaced(activeDialogueCustomerIndex);
		break;

	case DialogueAction::BuyerPurchaseAccepted:
		ApplyBuyerPurchaseTransaction(activeDialogueCustomerIndex);
		CustomerTakeCounterItem(activeDialogueCustomerIndex);
		break;

	case DialogueAction::BuyerPurchaseDeclined:
		CustomerTakeCounterItem(activeDialogueCustomerIndex);
		break;

	case DialogueAction::SellerPurchaseAccepted:
		ApplySellerPurchaseTransaction(activeDialogueCustomerIndex);
		// Do not take the item. Player bought it, so it remains on counter.
		break;

	case DialogueAction::SellerPurchaseDeclined:
		CustomerTakeCounterItem(activeDialogueCustomerIndex);
		break;

	default:
		break;
	}
}

void Game::DrawDialogue()
{
	if (!dialogueActive)
	{
		DrawGameplayPrompts();
		return;
	}

	if (customerDialogueManager != nullptr)
	{
		DrawDialogueManager(customerDialogueManager);

		DialogueNode* node = GetActiveDialogueNode();

		if (node != nullptr && dialogueTextComplete && !node->choices.empty())
		{
			for (int i = 0; i < (int)node->choices.size(); i++)
			{
				Color color = (i == selectedDialogueChoice) ? YELLOW : WHITE;

				DrawText(
					TextFormat(
						"%s %s",
						i == selectedDialogueChoice ? ">" : " ",
						node->choices[i].text.c_str()
					),
					100,
					GetScreenHeight() - 125 + i * 24,
					20,
					color
				);
			}
		}
	}

	if (inspectMode)
	{
		if (dialogueActive && IsSelfInspectDialogueActive())
		{
			DrawBottomPrompt(
				"Auto dialogue ON   |   Space / Enter to skip   |   Press F to stop inspecting",
				58
			);
		}
		else
		{
			DrawBottomPrompt(
				"Press F to stop inspecting   |   Left Mouse drag to rotate   |   Mouse wheel to zoom",
				58
			);
		}
	}
	else
	{
		const char* autoText = currentDialogueAutoPlayEnabled
			? "Auto: ON"
			: "Auto: OFF";

		if (IsCurrentDialogueCustomerDialogue())
		{
			DrawText(
				TextFormat(
					"Space/Enter: Select/Next   Up/Down: Choice   F: Toggle Auto (%s)   Left Alt: Close",
					autoText
				),
				80,
				GetScreenHeight() - 40,
				18,
				LIGHTGRAY
			);
		}
		else
		{
			DrawText(
				TextFormat(
					"Space/Enter: Skip/Next   %s   Left Alt: Close",
					autoText
				),
				80,
				GetScreenHeight() - 40,
				18,
				LIGHTGRAY
			);
		}
	}
}

void Game::StopAllCustomerRoaming()
{
	for (Customer& customer : customers)
	{
		customer.hasMoveTarget = false;

		customer.targetPOIIndex = -1;
		customer.destinationPOIIndex = -1;

		customer.poiRoute.clear();
		customer.poiRouteCursor = 0;

		customer.pathWaypoints.clear();
		customer.pathWaypointCursor = 0;

		customer.poiWaitTimer = 0.0f;
		customer.movementPauseTimer = 0.0f;
		customer.repathTimer = 0.0f;

		customer.SetAnimState(CustomerAnimState::Idle);
	}

	SyncCustomerBodiesToCustomers(GetFrameTime());
}

bool Game::IsCustomerStaticBlockedAt(Vector3 position) const
{
	if (CustomerWouldHitSceneProps(position))
		return true;

	if (CustomerWouldHitInstancedProps(position))
		return true;

	if (CustomerWouldHitBlockout(position))
		return true;

	return false;
}
bool Game::WorldToNavCell(Vector3 position, int& outX, int& outZ) const
{
	const CustomerNavGrid& grid = customerNavGrid;

	if (!grid.valid)
		return false;

	outX = (int)floorf((position.x - grid.minX) / grid.cellSize);
	outZ = (int)floorf((position.z - grid.minZ) / grid.cellSize);

	if (outX < 0 || outX >= grid.width) return false;
	if (outZ < 0 || outZ >= grid.height) return false;

	return true;
}

Vector3 Game::NavCellToWorld(int x, int z) const
{
	const CustomerNavGrid& grid = customerNavGrid;

	return {
		grid.minX + ((float)x + 0.5f) * grid.cellSize,
		0.0f,
		grid.minZ + ((float)z + 0.5f) * grid.cellSize
	};
}

bool Game::IsNavCellBlockedForCustomer(
	int x,
	int z,
	int selfCustomerIndex,
	bool includeDynamic
) const
{
	const CustomerNavGrid& grid = customerNavGrid;

	if (x < 0 || x >= grid.width) return true;
	if (z < 0 || z >= grid.height) return true;

	int index = z * grid.width + x;

	if (grid.blocked[index] != 0)
		return true;

	if (includeDynamic)
	{
		Vector3 world = NavCellToWorld(x, z);

		if (IsCustomerDynamicBlockedAt(selfCustomerIndex, world))
			return true;
	}

	return false;
}

bool Game::FindNearestOpenNavCell(
	int startX,
	int startZ,
	int& outX,
	int& outZ,
	int selfCustomerIndex,
	bool includeDynamic
) const
{
	for (int radius = 0; radius <= 8; radius++)
	{
		for (int dz = -radius; dz <= radius; dz++)
		{
			for (int dx = -radius; dx <= radius; dx++)
			{
				int x = startX + dx;
				int z = startZ + dz;

				if (!IsNavCellBlockedForCustomer(x, z, selfCustomerIndex, includeDynamic))
				{
					outX = x;
					outZ = z;
					return true;
				}
			}
		}
	}

	return false;
}

void Game::RebuildCustomerNavGrid()
{
	CustomerNavGrid& grid = customerNavGrid;

	float minX = FLT_MAX;
	float maxX = -FLT_MAX;
	float minZ = FLT_MAX;
	float maxZ = -FLT_MAX;

	bool hasPoint = false;

	auto expandPoint = [&](Vector3 p)
		{
			minX = fminf(minX, p.x);
			maxX = fmaxf(maxX, p.x);
			minZ = fminf(minZ, p.z);
			maxZ = fmaxf(maxZ, p.z);
			hasPoint = true;
		};

	auto expandBoxXZ = [&](Vector3 center, Vector3 half)
		{
			expandPoint({ center.x - half.x, center.y, center.z - half.z });
			expandPoint({ center.x + half.x, center.y, center.z + half.z });
		};

	// ----------------------------------------------------
	// Scene props
	// ----------------------------------------------------
	for (const SceneProp& prop : sceneProps)
	{
		if (!prop.visible) continue;
		if (!prop.hasCollision) continue;
		if (!prop.blocksPlayer) continue;

		Vector3 center = Vector3Add(
			prop.position,
			GetScenePropRotatedOffset(prop)
		);

		Vector3 half = {
			fabsf(prop.colliderSize.x * prop.scale.x) * 0.5f,
			fabsf(prop.colliderSize.y * prop.scale.y) * 0.5f,
			fabsf(prop.colliderSize.z * prop.scale.z) * 0.5f
		};

		expandBoxXZ(center, half);
	}

	// ----------------------------------------------------
	// Instanced props, for example gacha machines
	// ----------------------------------------------------
	for (const InstancedProp& prop : instancedProps)
	{
		if (!prop.visible) continue;
		if (!prop.hasCollision) continue;
		if (!prop.blocksPlayer) continue;

		if (prop.type != InstancePropType::GachaMachine)
			continue;

		Vector3 scaledSize = {
			fabsf(prop.colliderSize.x * prop.scale.x),
			fabsf(prop.colliderSize.y * prop.scale.y),
			fabsf(prop.colliderSize.z * prop.scale.z)
		};

		Vector3 half = {
			scaledSize.x * 0.5f,
			scaledSize.y * 0.5f,
			scaledSize.z * 0.5f
		};

		Quaternion rot = QuaternionFromEuler(
			prop.rotationDeg.x * DEG2RAD,
			prop.rotationDeg.y * DEG2RAD,
			prop.rotationDeg.z * DEG2RAD
		);

		Vector3 scaledOffset = {
			prop.colliderOffset.x * prop.scale.x,
			prop.colliderOffset.y * prop.scale.y,
			prop.colliderOffset.z * prop.scale.z
		};

		Vector3 rotatedOffset = Vector3RotateByQuaternion(scaledOffset, rot);
		Vector3 center = Vector3Add(prop.position, rotatedOffset);

		expandBoxXZ(center, half);
	}

	// ----------------------------------------------------
	// Blockout boxes
	// ----------------------------------------------------
	for (const BlockoutBox& box : blockoutBoxes)
	{
		if (!box.visible) continue;
		if (!box.hasCollision) continue;
		if (!box.blocksPlayer) continue;

		Vector3 half = {
			fabsf(box.size.x) * 0.5f,
			fabsf(box.size.y) * 0.5f,
			fabsf(box.size.z) * 0.5f
		};

		expandBoxXZ(box.position, half);
	}

	// ----------------------------------------------------
	// Customer POIs
	// ----------------------------------------------------
	for (const CustomerPOI& poi : customerPOIs)
	{
		if (!poi.enabled) continue;

		expandPoint(poi.position);
	}

	// ----------------------------------------------------
	// Current customers
	// ----------------------------------------------------
	for (const Customer& customer : customers)
	{
		expandPoint(customer.position);
	}

	if (!hasPoint)
	{
		minX = -10.0f;
		maxX = 10.0f;
		minZ = -10.0f;
		maxZ = 10.0f;
	}

	const float padding = 3.0f;

	grid.minX = minX - padding;
	grid.maxX = maxX + padding;
	grid.minZ = minZ - padding;
	grid.maxZ = maxZ + padding;

	grid.width = (int)ceilf((grid.maxX - grid.minX) / grid.cellSize) + 1;
	grid.height = (int)ceilf((grid.maxZ - grid.minZ) / grid.cellSize) + 1;

	grid.width = std::max(grid.width, 1);
	grid.height = std::max(grid.height, 1);

	grid.blocked.assign(grid.width * grid.height, 0);

	grid.valid = true;

	for (int z = 0; z < grid.height; z++)
	{
		for (int x = 0; x < grid.width; x++)
		{
			Vector3 world = NavCellToWorld(x, z);

			if (IsCustomerStaticBlockedAt(world))
			{
				grid.blocked[z * grid.width + x] = 1;
			}
		}
	}

	customerNavGridDirty = false;

	TraceLog(
		LOG_INFO,
		"Customer nav grid rebuilt: %i x %i, cellSize %.2f",
		grid.width,
		grid.height,
		grid.cellSize
	);
}

bool Game::IsCustomerDynamicBlockedAt(int selfCustomerIndex, Vector3 position) const
{
	if (CustomerWouldHitPlayer(position))
		return true;

	if (CustomerWouldHitOtherCustomers(selfCustomerIndex, position))
		return true;

	return false;
}

std::vector<Vector3> Game::FindCustomerAStarPath(
	int customerIndex,
	Vector3 start,
	Vector3 goal,
	bool includeDynamic
)
{
	if (customerNavGridDirty || !customerNavGrid.valid)
	{
		RebuildCustomerNavGrid();
	}

	std::vector<Vector3> empty;

	int startX = 0;
	int startZ = 0;
	int goalX = 0;
	int goalZ = 0;

	if (!WorldToNavCell(start, startX, startZ))
		return empty;

	if (!WorldToNavCell(goal, goalX, goalZ))
		return empty;

	if (!FindNearestOpenNavCell(startX, startZ, startX, startZ, customerIndex, false))
		return empty;

	if (!FindNearestOpenNavCell(goalX, goalZ, goalX, goalZ, customerIndex, false))
		return empty;

	const CustomerNavGrid& grid = customerNavGrid;
	const int total = grid.width * grid.height;
	const float INF = FLT_MAX;

	struct OpenNode
	{
		int x = 0;
		int z = 0;
		float f = 0.0f;
	};

	struct OpenCompare
	{
		bool operator()(const OpenNode& a, const OpenNode& b) const
		{
			return a.f > b.f;
		}
	};

	auto indexOf = [&](int x, int z)
		{
			return z * grid.width + x;
		};

	auto heuristic = [&](int x, int z)
		{
			float dx = (float)(goalX - x);
			float dz = (float)(goalZ - z);
			return sqrtf(dx * dx + dz * dz);
		};

	std::vector<float> gScore(total, INF);
	std::vector<int> cameFrom(total, -1);
	std::vector<unsigned char> closed(total, 0);

	std::priority_queue<OpenNode, std::vector<OpenNode>, OpenCompare> open;

	int startIndex = indexOf(startX, startZ);

	gScore[startIndex] = 0.0f;
	open.push({ startX, startZ, heuristic(startX, startZ) });

	const int dirs[8][2] = {
		{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
		{ 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 }
	};

	while (!open.empty())
	{
		OpenNode current = open.top();
		open.pop();

		int currentIndex = indexOf(current.x, current.z);

		if (closed[currentIndex])
			continue;

		closed[currentIndex] = 1;

		if (current.x == goalX && current.z == goalZ)
			break;

		for (int i = 0; i < 8; i++)
		{
			int nx = current.x + dirs[i][0];
			int nz = current.z + dirs[i][1];

			bool isGoal = (nx == goalX && nz == goalZ);

			if (IsNavCellBlockedForCustomer(
				nx,
				nz,
				customerIndex,
				includeDynamic && !isGoal
			))
			{
				continue;
			}

			// Prevent diagonal corner cutting.
			if (dirs[i][0] != 0 && dirs[i][1] != 0)
			{
				int sideX = current.x + dirs[i][0];
				int sideZ = current.z;

				int frontX = current.x;
				int frontZ = current.z + dirs[i][1];

				if (IsNavCellBlockedForCustomer(sideX, sideZ, customerIndex, false) ||
					IsNavCellBlockedForCustomer(frontX, frontZ, customerIndex, false))
				{
					continue;
				}
			}

			int nextIndex = indexOf(nx, nz);

			if (closed[nextIndex])
				continue;

			float moveCost = (dirs[i][0] != 0 && dirs[i][1] != 0) ? 1.4142f : 1.0f;
			float tentativeG = gScore[currentIndex] + moveCost;

			if (tentativeG < gScore[nextIndex])
			{
				cameFrom[nextIndex] = currentIndex;
				gScore[nextIndex] = tentativeG;

				float f = tentativeG + heuristic(nx, nz);
				open.push({ nx, nz, f });
			}
		}
	}

	int goalIndex = indexOf(goalX, goalZ);

	if (cameFrom[goalIndex] < 0 && goalIndex != startIndex)
	{
		return empty;
	}

	std::vector<Vector3> reversed;

	int node = goalIndex;

	while (node >= 0)
	{
		int x = node % grid.width;
		int z = node / grid.width;

		reversed.push_back(NavCellToWorld(x, z));

		if (node == startIndex)
			break;

		node = cameFrom[node];
	}

	std::reverse(reversed.begin(), reversed.end());

	if (reversed.empty())
		return empty;

	// Use exact POI only if it is actually walkable.
// If the POI is inside/too close to a collider, keep the nearest open nav cell.
	if (!IsCustomerStaticBlockedAt(goal))
	{
		reversed.back() = goal;
	}
	else
	{
		TraceLog(
			LOG_WARNING,
			"Customer POI goal is blocked. Using nearest open nav cell instead. goal=(%.2f %.2f %.2f)",
			goal.x,
			goal.y,
			goal.z
		);
	}

	return SmoothCustomerPath(reversed);
}

std::vector<Vector3> Game::SmoothCustomerPath(const std::vector<Vector3>& path) const
{
	if (path.size() <= 2)
		return path;

	std::vector<Vector3> result;

	int anchor = 0;
	result.push_back(path[0]);

	while (anchor < (int)path.size() - 1)
	{
		int best = anchor + 1;

		for (int test = (int)path.size() - 1; test > anchor + 1; test--)
		{
			if (!IsCustomerPathBlocked(path[anchor], path[test]))
			{
				best = test;
				break;
			}
		}

		result.push_back(path[best]);
		anchor = best;
	}

	return result;
}
void Game::DrawCustomerPOIEditorUI()
{
	if (!ImGui::CollapsingHeader("Customer Points of Interest"))
		return;

	if (ImGui::Checkbox("Enable Customer Roaming", &customerRoamingEnabled))
	{
		if (!customerRoamingEnabled)
		{
			StopAllCustomerRoaming();
		}
	}

	ImGui::Separator();

	if (ImGui::Button("Add POI At Player"))
	{
		PushUndoSnapshot();

		CustomerPOI poi;
		poi.id = TextFormat("poi_%i", (int)customerPOIs.size());
		poi.position = player.m_pos;
		poi.position.y = 0.0f;

		customerPOIs.push_back(poi);
		selectedCustomerPOI = (int)customerPOIs.size() - 1;

		customerNavGridDirty = true;
		StopAllCustomerRoaming();
	}

	ImGui::SameLine();

	if (ImGui::Button("Save POIs"))
	{
		SaveCustomerPOIs();
	}

	ImGui::SameLine();

	if (ImGui::Button("Load POIs"))
	{
		PushUndoSnapshot();

		LoadCustomerPOIs();
		customerNavGridDirty = true;
		StopAllCustomerRoaming();
	}

	ImGui::Separator();

	for (int i = 0; i < (int)customerPOIs.size(); i++)
	{
		CustomerPOI& poi = customerPOIs[i];

		std::string label = poi.id + " [" + poi.group + "]";

		if (!poi.enabled)
			label += " disabled";

		if (!poi.stopPoint)
			label += " waypoint";

		if (ImGui::Selectable(label.c_str(), selectedCustomerPOI == i))
		{
			selectedCustomerPOI = i;
		}
	}

	if (selectedCustomerPOI < 0 || selectedCustomerPOI >= (int)customerPOIs.size())
		return;

	ImGui::Separator();

	CustomerPOI& poi = customerPOIs[selectedCustomerPOI];

	char idBuffer[128] = {};
	strncpy_s(idBuffer, poi.id.c_str(), sizeof(idBuffer) - 1);

	if (ImGui::InputText("ID", idBuffer, sizeof(idBuffer)))
	{
		poi.id = idBuffer;
	}
	PushUndoIfItemActivated();

	char groupBuffer[128] = {};
	strncpy_s(groupBuffer, poi.group.c_str(), sizeof(groupBuffer) - 1);

	if (ImGui::InputText("Group", groupBuffer, sizeof(groupBuffer)))
	{
		poi.group = groupBuffer;
		StopAllCustomerRoaming();
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat3("Position", &poi.position.x, 0.05f))
	{
		customerNavGridDirty = true;
		StopAllCustomerRoaming();
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat("Radius", &poi.radius, 0.05f, 0.05f, 5.0f))
	{
		StopAllCustomerRoaming();
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat("Wait Min", &poi.waitSecondsMin, 0.1f, 0.0f, 30.0f))
	{
		if (poi.waitSecondsMax < poi.waitSecondsMin)
			poi.waitSecondsMax = poi.waitSecondsMin;

		StopAllCustomerRoaming();
	}
	PushUndoIfItemActivated();

	if (ImGui::DragFloat("Wait Max", &poi.waitSecondsMax, 0.1f, 0.0f, 30.0f))
	{
		if (poi.waitSecondsMin > poi.waitSecondsMax)
			poi.waitSecondsMin = poi.waitSecondsMax;

		StopAllCustomerRoaming();
	}
	PushUndoIfItemActivated();

	if (ImGui::DragInt("Capacity", &poi.capacity, 1.0f, 1, 20))
	{
		StopAllCustomerRoaming();
	}
	PushUndoIfItemActivated();

	if (ImGui::Checkbox("Exclusive", &poi.exclusive))
	{
		StopAllCustomerRoaming();
	}
	PushUndoIfItemActivated();

	int kindValue = (int)poi.kind;

	const char* kindNames[] = {
		"Generic",
		"Entry",
		"Exit",
		"BrowseItem",
		"Counter",
		"QueueSlot"
	};

	if (ImGui::Combo("Kind", &kindValue, kindNames, IM_ARRAYSIZE(kindNames)))
	{
		poi.kind = (CustomerPOIKind)kindValue;
		customerNavGridDirty = true;
		StopAllCustomerRoaming();
	}
	PushUndoIfItemActivated();

	if (ImGui::DragInt("Queue Order", &poi.queueOrder, 1.0f, -1, 20))
	{
		StopAllCustomerRoaming();
	}
	PushUndoIfItemActivated();

	if (ImGui::Checkbox("Use Facing Direction", &poi.useFacingDirection))
	{
		StopAllCustomerRoaming();
	}
	PushUndoIfItemActivated();

	if (poi.useFacingDirection)
	{
		if (ImGui::DragFloat("Facing Yaw", &poi.facingYawDeg, 1.0f, -360.0f, 360.0f))
		{
			StopAllCustomerRoaming();
		}
		PushUndoIfItemActivated();

		if (ImGui::Button("Face Toward Player"))
		{
			PushUndoSnapshot();

			poi.facingYawDeg = GetYawToTargetXZ(poi.position, player.m_pos);
			StopAllCustomerRoaming();
		}

		ImGui::SameLine();

		if (ImGui::Button("Use Player Yaw"))
		{
			PushUndoSnapshot();

			poi.facingYawDeg = player.yaw;
			StopAllCustomerRoaming();
		}
	}

	if (ImGui::Checkbox("Enabled", &poi.enabled))
	{
		StopAllCustomerRoaming();
	}
	PushUndoIfItemActivated();

	if (ImGui::Checkbox("Stop Point", &poi.stopPoint))
	{
		StopAllCustomerRoaming();
	}
	PushUndoIfItemActivated();

	if (ImGui::Button("Move POI To Player"))
	{
		PushUndoSnapshot();

		poi.position = player.m_pos;
		poi.position.y = 0.0f;

		customerNavGridDirty = true;
		StopAllCustomerRoaming();
	}

	ImGui::SameLine();

	if (ImGui::Button("Delete POI"))
	{
		PushUndoSnapshot();

		customerPOIs.erase(customerPOIs.begin() + selectedCustomerPOI);
		selectedCustomerPOI = -1;

		customerNavGridDirty = true;
		StopAllCustomerRoaming();
	}
}
bool Game::CustomerWouldHitSceneProps(Vector3 position) const
{
	BoundingBox customerBox = MakeCustomerCollisionBoxAt(position);

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		if (hasHeldBody && i == heldScenePropIndex)
			continue;

		const SceneProp& prop = sceneProps[i];

		if (!prop.visible)
			continue;

		if (!prop.hasCollision)
			continue;

		if (!prop.blocksPlayer)
			continue;

		Vector3 center = Vector3Add(
			prop.position,
			GetScenePropRotatedOffset(prop)
		);

		Vector3 half = {
			fabsf(prop.colliderSize.x * prop.scale.x) * 0.5f,
			fabsf(prop.colliderSize.y * prop.scale.y) * 0.5f,
			fabsf(prop.colliderSize.z * prop.scale.z) * 0.5f
		};

		BoundingBox propBox{};
		propBox.min = { center.x - half.x, center.y - half.y, center.z - half.z };
		propBox.max = { center.x + half.x, center.y + half.y, center.z + half.z };

		if (CheckCollisionBoxes(customerBox, propBox))
			return true;
	}

	return false;
}

bool Game::CustomerWouldHitBlockout(Vector3 position) const
{
	BoundingBox customerBox = MakeCustomerCollisionBoxAt(position);

	for (const BlockoutBox& boxData : blockoutBoxes)
	{
		if (!boxData.hasCollision)
			continue;

		if (!boxData.blocksCustomers)
			continue;

		Vector3 half = {
			fabsf(boxData.size.x) * 0.5f,
			fabsf(boxData.size.y) * 0.5f,
			fabsf(boxData.size.z) * 0.5f
		};

		BoundingBox blockBox{};
		blockBox.min = {
			boxData.position.x - half.x,
			boxData.position.y - half.y,
			boxData.position.z - half.z
		};

		blockBox.max = {
			boxData.position.x + half.x,
			boxData.position.y + half.y,
			boxData.position.z + half.z
		};

		if (CheckCollisionBoxes(customerBox, blockBox))
			return true;
	}

	return false;
}
bool Game::CustomerWouldHitPlayer(Vector3 position) const
{
	BoundingBox customerBox = MakeCustomerCollisionBoxAt(position);
	BoundingBox playerBox = MakePlayerCollisionBoxAt(player.m_pos);

	return CheckCollisionBoxes(customerBox, playerBox);
}

bool Game::CustomerWouldHitOtherCustomers(int customerIndex, Vector3 position) const
{
	BoundingBox customerBox = MakeCustomerCollisionBoxAt(position);

	for (int i = 0; i < (int)customers.size(); i++)
	{
		if (i == customerIndex)
			continue;

		const Customer& other = customers[i];

		BoundingBox otherBox = MakeCustomerCollisionBoxAt(other.position);

		if (CheckCollisionBoxes(customerBox, otherBox))
			return true;
	}

	return false;
}

bool Game::IsCustomerPositionBlocked(int customerIndex, Vector3 position) const
{
	if (CustomerWouldHitSceneProps(position))
		return true;

	if (CustomerWouldHitInstancedProps(position))
		return true;

	if (CustomerWouldHitBlockout(position))
		return true;

	if (CustomerWouldHitPlayer(position))
		return true;

	if (CustomerWouldHitOtherCustomers(customerIndex, position))
		return true;

	return false;
}

void Game::UpdateCustomersWithCollision(float dt)
{
	std::vector<Vector3> oldPositions;
	oldPositions.reserve(customers.size());

	for (const Customer& customer : customers)
	{
		oldPositions.push_back(customer.position);
	}


	auto findNearbyOpenPosition = [&](int customerIndex, Vector3 start, Vector3& outPosition) -> bool
		{
			const float radii[] = {
				0.25f,
				0.45f,
				0.70f,
				1.00f,
				1.35f,
				1.75f,
				2.20f
			};

			const Vector3 dirs[] = {
				{ 1.0f, 0.0f, 0.0f },
				{ -1.0f, 0.0f, 0.0f },
				{ 0.0f, 0.0f, 1.0f },
				{ 0.0f, 0.0f, -1.0f },
				{ 0.707f, 0.0f, 0.707f },
				{ -0.707f, 0.0f, 0.707f },
				{ 0.707f, 0.0f, -0.707f },
				{ -0.707f, 0.0f, -0.707f }
			};

			for (float r : radii)
			{
				for (Vector3 dir : dirs)
				{
					Vector3 candidate = Vector3Add(
						start,
						Vector3Scale(dir, r)
					);

					candidate.y = start.y;

					if (IsCustomerPositionBlocked(customerIndex, candidate))
						continue;

					outPosition = candidate;
					return true;
				}
			}

			return false;
		};

	auto findOpenPositionAwayFrom = [&](
		int customerIndex,
		Vector3 start,
		Vector3 awayFrom,
		Vector3& outPosition
		) -> bool
		{
			Vector3 away = Vector3Subtract(start, awayFrom);
			away.y = 0.0f;

			if (Vector3Length(away) <= 0.001f)
			{
				// Stable fallback direction based on index.
				float angle = (float)(customerIndex * 67) * DEG2RAD;

				away = {
					sinf(angle),
					0.0f,
					cosf(angle)
				};
			}
			else
			{
				away = Vector3Normalize(away);
			}

			Vector3 right = {
				away.z,
				0.0f,
				-away.x
			};

			const float distances[] = {
				0.35f,
				0.55f,
				0.80f,
				1.10f,
				1.45f,
				1.85f,
				2.25f
			};

			Vector3 dirs[] = {
				away,
				Vector3Normalize(Vector3Add(away, Vector3Scale(right, 0.65f))),
				Vector3Normalize(Vector3Subtract(away, Vector3Scale(right, 0.65f))),
				right,
				Vector3Scale(right, -1.0f),
				Vector3Scale(away, -1.0f)
			};

			for (float distance : distances)
			{
				for (Vector3 dir : dirs)
				{
					Vector3 candidate = Vector3Add(
						start,
						Vector3Scale(dir, distance)
					);

					candidate.y = start.y;

					if (IsCustomerPositionBlocked(customerIndex, candidate))
						continue;

					outPosition = candidate;
					return true;
				}
			}

			return false;
		};

	auto resetCustomerAfterSeparation = [&](int customerIndex)
		{
			if (customerIndex < 0 || customerIndex >= (int)customers.size())
				return;

			Customer& customer = customers[customerIndex];

			customer.hasMoveTarget = false;
			customer.pathWaypoints.clear();
			customer.pathWaypointCursor = 0;

			customer.movementPauseTimer =
				0.10f + (float)GetRandomValue(0, 15) / 100.0f;

			customer.poiWaitTimer = 0.0f;
			customer.repathTimer = customerRepathInterval;

			customer.SetAnimState(CustomerAnimState::Idle);

			if (customer.destinationPOIIndex >= 0)
			{
				StartCustomerRouteToPOI(
					customerIndex,
					customer.destinationPOIIndex
				);
			}
		};

	auto resolveCustomerOverlaps = [&]()
		{
			for (int a = 0; a < (int)customers.size(); a++)
			{
				Customer& customerA = customers[a];

				if (customerA.pendingDespawn)
					continue;

				if (customerA.editorFrozen)
					continue;

				for (int b = a + 1; b < (int)customers.size(); b++)
				{
					Customer& customerB = customers[b];

					if (customerB.pendingDespawn)
						continue;

					if (customerB.editorFrozen)
						continue;

					BoundingBox boxA = MakeCustomerCollisionBoxAt(customerA.position);
					BoundingBox boxB = MakeCustomerCollisionBoxAt(customerB.position);

					if (!CheckCollisionBoxes(boxA, boxB))
						continue;

					// Pick one customer to move.
					// Prefer moving the customer that is currently moving.
					int mover = -1;
					int anchor = -1;

					bool aDialogue = IsCustomerInActiveDialogue(a);
					bool bDialogue = IsCustomerInActiveDialogue(b);

					if (aDialogue && !bDialogue)
					{
						mover = b;
						anchor = a;
					}
					else if (bDialogue && !aDialogue)
					{
						mover = a;
						anchor = b;
					}
					else if (customerA.hasMoveTarget && !customerB.hasMoveTarget)
					{
						mover = a;
						anchor = b;
					}
					else if (customerB.hasMoveTarget && !customerA.hasMoveTarget)
					{
						mover = b;
						anchor = a;
					}
					else
					{
						// Stable tie-breaker so both customers do not keep choosing each other.
						mover = b;
						anchor = a;
					}

					Vector3 separatedPosition{};

					if (findOpenPositionAwayFrom(
						mover,
						customers[mover].position,
						customers[anchor].position,
						separatedPosition
					))
					{
						customers[mover].position = separatedPosition;
						resetCustomerAfterSeparation(mover);
						continue;
					}

					if (findNearbyOpenPosition(
						mover,
						customers[mover].position,
						separatedPosition
					))
					{
						customers[mover].position = separatedPosition;
						resetCustomerAfterSeparation(mover);
						continue;
					}
				}
			}
		};


	resolveCustomerOverlaps();


	for (int i = 0; i < (int)customers.size(); i++)
	{
		Customer& customer = customers[i];

		if (customer.editorFrozen)
		{
			customer.hasMoveTarget = false;
			customer.SetAnimState(CustomerAnimState::Idle);
			continue;
		}

		if (IsCustomerInActiveDialogue(i))
		{
			customer.hasMoveTarget = false;
			customer.pathWaypoints.clear();
			customer.pathWaypointCursor = 0;
			customer.movementPauseTimer = 0.0f;

			// Update animation once, but skip collision response.
			customer.Update(dt);
			continue;
		}

		Vector3 oldPosition = oldPositions[i];

		// If customer is already inside a static object, unstuck first.
		if (IsCustomerStaticBlockedAt(oldPosition))
		{
			Vector3 unstuckPosition{};

			if (findNearbyOpenPosition(i, oldPosition, unstuckPosition))
			{
				customer.position = unstuckPosition;
				oldPosition = unstuckPosition;
			}

			customer.hasMoveTarget = false;
			customer.pathWaypoints.clear();
			customer.pathWaypointCursor = 0;

			customer.movementPauseTimer = 0.0f;
			customer.poiWaitTimer = 0.35f;

			customer.SetAnimState(CustomerAnimState::Idle);

			if (customer.destinationPOIIndex >= 0)
			{
				StartCustomerRouteToPOI(
					i,
					customer.destinationPOIIndex
				);
			}

			continue;
		}

		customer.Update(dt);

		bool hitStatic =
			CustomerWouldHitSceneProps(customer.position) ||
			CustomerWouldHitInstancedProps(customer.position) ||
			CustomerWouldHitBlockout(customer.position);

		bool hitPlayer = CustomerWouldHitPlayer(customer.position);
		bool hitOtherCustomer = CustomerWouldHitOtherCustomers(i, customer.position);

		bool hitDynamic = hitPlayer || hitOtherCustomer;

		if (!hitStatic && !hitDynamic)
			continue;

		customer.position = oldPosition;

		if (hitStatic)
		{
			Vector3 unstuckPosition{};

			if (findNearbyOpenPosition(i, customer.position, unstuckPosition))
			{
				customer.position = unstuckPosition;

				customer.hasMoveTarget = false;
				customer.pathWaypoints.clear();
				customer.pathWaypointCursor = 0;

				customer.movementPauseTimer = 0.0f;
				customer.poiWaitTimer = 0.05f;

				if (customer.destinationPOIIndex >= 0)
				{
					StartCustomerRouteToPOI(
						i,
						customer.destinationPOIIndex
					);
				}

				continue;
			}

			if (TryCustomerSidestep(i))
			{
				customer.movementPauseTimer = 0.05f;
				continue;
			}

			customer.hasMoveTarget = false;
			customer.pathWaypoints.clear();
			customer.pathWaypointCursor = 0;

			customer.movementPauseTimer = 0.35f;
			customer.poiWaitTimer = 0.35f;

			customer.SetAnimState(CustomerAnimState::Idle);
		}
		else
		{
			// Player blocker:
			// Do not teleport / unstuck the customer because of player overlap.
			// Let the player-side collision resolver slide the player around the customer.
			if (hitPlayer)
			{
				customer.position = oldPosition;

				customer.movementPauseTimer = fmaxf(customer.movementPauseTimer, 0.15f);
				customer.poiWaitTimer = 0.0f;

				customer.SetAnimState(CustomerAnimState::Idle);

				continue;
			}

			// Other customer blocker:
			// Keep the old separation logic for customer-vs-customer only.
			Vector3 unstuckPosition{};

			if (findNearbyOpenPosition(i, customer.position, unstuckPosition))
			{
				customer.position = unstuckPosition;

				customer.hasMoveTarget = false;
				customer.pathWaypoints.clear();
				customer.pathWaypointCursor = 0;

				customer.movementPauseTimer = 0.05f;
				customer.poiWaitTimer = 0.0f;
				customer.repathTimer = customerRepathInterval;

				if (customer.destinationPOIIndex >= 0)
				{
					StartCustomerRouteToPOI(
						i,
						customer.destinationPOIIndex
					);
				}

				continue;
			}

			if (TryCustomerSidestep(i))
			{
				customer.movementPauseTimer = 0.08f;
				customer.repathTimer = 0.0f;
				continue;
			}

			customer.movementPauseTimer =
				0.20f + (float)GetRandomValue(0, 25) / 100.0f;

			customer.SetAnimState(CustomerAnimState::Idle);
		}
	}


	resolveCustomerOverlaps();
}

void Game::BuildCustomerTradeItemCatalog()
{
	customerTradeItems.clear();

	auto addTradeItem =
		[&](
			const std::string& id,
			const std::string& displayName,
			const std::string& itemTag,
			int buyPriceYen,
			int sellPriceYen,
			bool availableFromSellers,
			bool availableForBuyers,
			const std::string& sellerIntro,
			const std::string& sellerCondition,
			const std::string& sellerPriceHope,
			const std::string& buyerCounterLine,
			std::vector<std::string> inspectLines
			)
		{
			CustomerTradeItemDef item{};

			item.id = id;
			item.displayName = displayName;

			// Keep fallback as gBoy for now.
			// Tagged scene props will override the visual model later.
			item.model = &gBoy;

			item.itemTag = itemTag;

			item.baseOfferYen = buyPriceYen;
			item.sellPriceYen = sellPriceYen;

			item.availableFromSellers = availableFromSellers;
			item.availableForBuyers = availableForBuyers;

			item.sellerIntroLine = sellerIntro;
			item.sellerConditionLine = sellerCondition;
			item.sellerPriceHopeLine = sellerPriceHope;
			item.buyerCounterLine = buyerCounterLine;

			item.inspectLines = inspectLines;

			customerTradeItems.push_back(item);
		};

	// =========================================================
	// PS4 GAMES FROM TRADE-IN POSTER
	// These can be both traded in by sellers and bought by buyers.
	// baseOfferYen = cash value / shop payout
	// sellPriceYen = store credit value for now
	// =========================================================

	addTradeItem(
		"ps4_spiderman",
		"PS4 Spider-Man",
		"ps4_game",
		1850,
		2000,
		true,
		true,
		"Hi. I brought in a PS400 Spider-Man game. Are you taking trade-ins today?",
		"The case is clean, and the disc should still be in good condition.",
		"I saw similar games still have some value, so I hope the offer is fair.",
		"I would like to buy this PS400 Spider-Man game. Can you scan it?",
		{
			"The PS400 case is in decent condition.",
			"The cover art looks complete.",
			"The disc should be checked for scratches before pricing.",
			"Trade-in cash value looks close to 1850 yen."
		}
	);

	addTradeItem(
		"ps4_red_dead_2",
		"PS4 Red Dead Redemption II",
		"ps4_game",
		3700,
		3850,
		true,
		true,
		"Hello. I want to trade in this PS400 Red Dead Redemption II.",
		"The case and cover are still good, and the disc looks clean.",
		"I know this title still has good demand, so I hope the value is higher.",
		"I picked up this PS400 Red Dead Redemption II. Can you scan it for me?",
		{
			"The case is in good condition.",
			"The title is still popular, so resale value should be stronger.",
			"Cash trade-in value looks close to 3700 yen."
		}
	);

	addTradeItem(
		"ps4_until_dawn",
		"PS4 Until Dawn",
		"ps4_game",
		2150,
		2300,
		true,
		true,
		"Hi. I brought this PS400 Until Dawn game for trade-in.",
		"The cover has some wear, but the disc should be alright.",
		"I am not expecting too much, but I hope it still has value.",
		"I found this PS400 Until Dawn game. Can you scan it?",
		{
			"The case has some age, but it is still presentable.",
			"The disc condition should be checked under light.",
			"Cash trade-in value looks close to 2150 yen."
		}
	);

	addTradeItem(
		"ps4_shadow_colossus",
		"PS4 Shadow of the Colossus",
		"ps4_game",
		1920,
		2070,
		true,
		true,
		"Hello. I would like to trade in this PS400 Shadow of the Colossus.",
		"The case is still complete, and the artwork looks good.",
		"I heard this title still has fans, so I hope the offer is reasonable.",
		"I want to buy this PS400 Shadow of the Colossus. Can you check the price?",
		{
			"The case and artwork are complete.",
			"This title may appeal to collectors or fans of classic games.",
			"Cash trade-in value looks close to 1920 yen."
		}
	);

	// =========================================================
	// SELLER-ONLY CONSOLES FOR NOW
	// Sellers can bring these to the shop.
	// =========================================================

	addTradeItem(
		"gameboy_console",
		"Game Boy Console",
		"retro_console",
		3000,
		4800,
		true,
		false,
		"Hi. I brought this old Game Boy console. Are you interested in buying it?",
		"The shell is a little worn, but the buttons and screen still look usable.",
		"I know old handheld consoles can still sell if they are working.",
		"",
		{
			"The shell has light wear from age.",
			"The screen should be checked for scratches and dead pixels.",
			"The buttons feel important to test before giving a final price.",
			"A working unit could be worth around 3000 yen as a trade-in."
		}
	);

	addTradeItem(
		"nes_console",
		"NES Console",
		"retro_console",
		5000,
		7800,
		true,
		false,
		"Hello. I have an old NES console I want to sell.",
		"It has been stored for a long time, but the body still looks complete.",
		"If it powers on, I think it should be worth a decent amount.",
		"",
		{
			"The console body looks aged but complete.",
			"The ports should be checked carefully.",
			"If it powers on, this could be a stronger trade-in item.",
			"A fair cash offer could be around 5000 yen."
		}
	);

	// =========================================================
	// BUYER-ONLY SHOP ITEMS FOR NOW
	// Buyers can pick these up from shelves and bring them to counter.
	// =========================================================

	addTradeItem(
		"ps5_console",
		"PS5 Console",
		"current_console",
		30000,
		48000,
		false,
		true,
		"",
		"",
		"",
		"I would like to buy this PS500 console. Can you scan it?",
		{
			"The console looks modern and high value.",
			"The item should be checked for accessories and box condition.",
			"The selling price is set at 48000 yen for now."
		}
	);

	addTradeItem(
		"antique_game_cartridge",
		"Antique Game Cartridge",
		"retro_cartridge",
		2500,
		6500,
		false,
		true,
		"",
		"",
		"",
		"I found this antique game cartridge. Can you check the price?",
		{
			"The cartridge looks old and collectible.",
			"The label condition matters a lot for value.",
			"If it is authentic, it can sell for around 6500 yen."
		}
	);
}

const Game::CustomerTradeItemDef* Game::FindCustomerTradeItemDef(
	const std::string& itemId
) const
{
	for (const CustomerTradeItemDef& item : customerTradeItems)
	{
		if (item.id == itemId)
			return &item;
	}

	return nullptr;
}

bool Game::DoesPickupCategoryMatchCustomer(
	PickupItemCategory category,
	bool seller
) const
{
	if (category == PickupItemCategory::Both)
		return true;

	if (seller)
		return category == PickupItemCategory::Selling;

	return category == PickupItemCategory::ForSale;
}

int Game::GetSellerBuyPriceYen(const CustomerTradeItemDef& item) const
{
	return item.baseOfferYen;
}

int Game::GetBuyerSellPriceYen(const CustomerTradeItemDef& item) const
{
	if (item.sellPriceYen > 0)
		return item.sellPriceYen;

	return item.baseOfferYen + 1200;
}

int Game::GetSellerBuyPriceYenForCustomer(int customerIndex) const
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return 0;

	const Customer& customer = customers[customerIndex];

	const CustomerTradeItemDef* item = FindCustomerTradeItemDef(
		customer.tradeItemId
	);

	if (item == nullptr)
		return 0;

	return GetSellerBuyPriceYen(*item);
}

int Game::GetBuyerSellPriceYenForCustomer(int customerIndex) const
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return 0;

	const Customer& customer = customers[customerIndex];

	const CustomerTradeItemDef* item = FindCustomerTradeItemDef(
		customer.tradeItemId
	);

	if (item == nullptr)
		return 0;

	return GetBuyerSellPriceYen(*item);
}

void Game::ApplyBuyerPurchaseTransaction(int customerIndex)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	Customer& customer = customers[customerIndex];

	if (customer.transactionApplied)
		return;

	int amount = GetBuyerSellPriceYenForCustomer(customerIndex);

	if (amount <= 0)
		return;

	int beforeBudget = storeBudgetYen;
	storeBudgetYen += amount;
	buyerSalesToday += amount;

	TraceLog(
		LOG_INFO,
		"BUYER SALE customer=%i item=%s amount=%i before=%i after=%i",
		customerIndex,
		customer.tradeItemId.c_str(),
		amount,
		beforeBudget,
		storeBudgetYen
	);

	customer.transactionApplied = true;
	lastTransactionDeltaYen = amount;
	lastTransactionText = TextFormat("+\xC2\xA5%i sale", amount);

	PlaySfx("cash");
}

void Game::ApplySellerPurchaseTransaction(int customerIndex)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	Customer& customer = customers[customerIndex];

	if (customer.transactionApplied)
		return;

	int amount = GetSellerBuyPriceYenForCustomer(customerIndex);

	if (amount <= 0)
		return;

	int beforeBudget = storeBudgetYen;
	storeBudgetYen -= amount;

	sellerBuySpendToday += amount;
	sellerPurchasesToday++;

	TraceLog(
		LOG_INFO,
		"SELLER TRADE-IN customer=%i item=%s amount=%i before=%i after=%i",
		customerIndex,
		customer.tradeItemId.c_str(),
		amount,
		beforeBudget,
		storeBudgetYen
	);

	customer.transactionApplied = true;
	lastTransactionDeltaYen = -amount;
	lastTransactionText = TextFormat("-\xC2\xA5%i trade-in", amount);

	PlaySfx("cash");
}

bool Game::DidPassCurrentDay() const
{
	StoreDayConfig cfg = GetCurrentStoreDayConfig();

	bool customerTargetMet =
		customersCompletedToday >= cfg.customerTarget;

	bool buyerSalesMet =
		buyerSalesToday >= cfg.minBuyerSalesYen;

	bool sellerBuyMet =
		sellerBuySpendToday >= cfg.minSellerBuyYen;

	return customerTargetMet && buyerSalesMet && sellerBuyMet;
}

int Game::FindTradeItemTemplateSceneProp(
	const std::string& tradeItemId,
	bool seller
) const
{
	if (tradeItemId.empty())
		return -1;

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		const SceneProp& prop = sceneProps[i];

		if (!prop.canPickup)
			continue;

		if (prop.model == nullptr)
			continue;

		// Do not use already spawned customer counter items as templates.
		if (prop.placedByCustomer)
			continue;

		if (prop.tradeItemId != tradeItemId)
			continue;

		if (!DoesPickupCategoryMatchCustomer(prop.pickupCategory, seller))
			continue;

		TraceLog(
			LOG_INFO,
			"Found trade template: tradeItemId=%s seller=%i propIndex=%i name=%s category=%i",
			tradeItemId.c_str(),
			seller ? 1 : 0,
			i,
			prop.name.c_str(),
			(int)prop.pickupCategory
		);

		return i;
	}

	return -1;
}

bool Game::HasSellerTradeItemBeenUsedToday(const std::string& itemId) const
{
	return std::find(
		sellerTradeItemsUsedToday.begin(),
		sellerTradeItemsUsedToday.end(),
		itemId
	) != sellerTradeItemsUsedToday.end();
}

const Game::CustomerTradeItemDef*
Game::PickSellerTradeItemDefNoRepeatForToday() const
{
	std::vector<const CustomerTradeItemDef*> preferredUnused;
	std::vector<const CustomerTradeItemDef*> allUnused;
	std::vector<const CustomerTradeItemDef*> preferredAll;
	std::vector<const CustomerTradeItemDef*> allAllowed;

	for (const CustomerTradeItemDef& item : customerTradeItems)
	{
		if (!item.availableFromSellers)
			continue;

		allAllowed.push_back(&item);

		bool hasTemplate =
			FindTradeItemTemplateSceneProp(item.id, true) >= 0;

		if (hasTemplate)
			preferredAll.push_back(&item);

		if (!HasSellerTradeItemBeenUsedToday(item.id))
		{
			allUnused.push_back(&item);

			if (hasTemplate)
				preferredUnused.push_back(&item);
		}
	}

	auto pickFrom = [](const std::vector<const CustomerTradeItemDef*>& list)
		-> const CustomerTradeItemDef*
		{
			if (list.empty())
				return nullptr;

			int index = GetRandomValue(0, (int)list.size() - 1);
			return list[index];
		};

	// Best case: unused seller item with actual tagged scene prop.
	if (!preferredUnused.empty())
		return pickFrom(preferredUnused);

	// Next: unused seller item.
	if (!allUnused.empty())
		return pickFrom(allUnused);

	// Only now allow duplicates.
	if (!preferredAll.empty())
		return pickFrom(preferredAll);

	if (!allAllowed.empty())
		return pickFrom(allAllowed);

	return nullptr;
}

const Game::CustomerTradeItemDef* Game::PickRandomTradeItemDefForCustomer(
	bool seller
) const
{
	std::vector<const CustomerTradeItemDef*> taggedMatches;
	std::vector<const CustomerTradeItemDef*> allowedMatches;

	for (const CustomerTradeItemDef& item : customerTradeItems)
	{
		bool allowed = seller
			? item.availableFromSellers
			: item.availableForBuyers;

		if (!allowed)
			continue;

		allowedMatches.push_back(&item);

		// Prefer items that actually have a tagged scene prop template.
		int templateIndex = FindTradeItemTemplateSceneProp(
			item.id,
			seller
		);

		if (templateIndex >= 0)
		{
			taggedMatches.push_back(&item);
		}
	}

	if (!taggedMatches.empty())
	{
		int index = GetRandomValue(
			0,
			(int)taggedMatches.size() - 1
		);

		return taggedMatches[index];
	}

	if (!allowedMatches.empty())
	{
		int index = GetRandomValue(
			0,
			(int)allowedMatches.size() - 1
		);

		return allowedMatches[index];
	}

	return nullptr;
}

const Game::CustomerTradeItemDef* Game::PickRandomCustomerTradeItemDef() const
{
	if (customerTradeItems.empty())
		return nullptr;

	int index = GetRandomValue(
		0,
		(int)customerTradeItems.size() - 1
	);

	return &customerTradeItems[index];
}

std::string Game::GetSellerScriptIdForItem(
	const std::string& itemId,
	const std::string& part
) const
{
	return "seller_" + itemId + "_" + part;
}

std::string Game::GetBuyerCounterScriptIdForItem(
	const std::string& itemId
) const
{
	return "buyer_" + itemId + "_purchase";
}

void Game::AssignTradeItemToCustomer(
	int customerIndex,
	bool seller
)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	Customer& customer = customers[customerIndex];

	if (customer.tradeItemChosen && !customer.tradeItemId.empty())
		return;

	const CustomerTradeItemDef* item = seller
		? PickSellerTradeItemDefNoRepeatForToday()
		: PickRandomTradeItemDefForCustomer(false);

	if (item == nullptr)
	{
		TraceLog(LOG_WARNING, "No customer trade items available.");
		return;
	}

	customer.tradeItemId = item->id;
	customer.tradeItemChosen = true;

	if (seller && !item->id.empty())
	{
		if (!HasSellerTradeItemBeenUsedToday(item->id))
		{
			sellerTradeItemsUsedToday.push_back(item->id);
		}
	}

	if (seller)
	{
		customer.dialogueScriptId = GetSellerScriptIdForItem(
			item->id,
			"part1"
		);
	}
	else
	{
		customer.dialogueScriptId = GetBuyerCounterScriptIdForItem(
			item->id
		);
	}

	TraceLog(
		LOG_INFO,
		"Assigned trade item '%s' to customer %i.",
		customer.tradeItemId.c_str(),
		customerIndex
	);
}

void Game::BuildItemPlacementSpots()
{
	itemPlacementSpots.clear();

	{
		ItemPlacementSpot spot{};
		spot.id = "counter_offer_0";
		spot.kind = ItemPlacementSpotKind::CounterOffer;
		spot.position = { 0.0f, 1.08f, -2.35f };
		spot.rotationDeg = { 0.0f, 180.0f, 0.0f };
		spot.snapRadius = 0.65f;
		spot.allowPlayerDrop = true;
		spot.allowCustomerPlace = true;
		itemPlacementSpots.push_back(spot);
	}

	{
		ItemPlacementSpot spot{};
		spot.id = "counter_scan_0";
		spot.kind = ItemPlacementSpotKind::CounterScan;
		spot.position = { 0.55f, 1.08f, -2.35f };
		spot.rotationDeg = { 0.0f, 180.0f, 0.0f };
		spot.snapRadius = 0.65f;
		spot.allowPlayerDrop = true;
		spot.allowCustomerPlace = false;
		itemPlacementSpots.push_back(spot);
	}

	{
		ItemPlacementSpot spot{};
		spot.id = "shelf_slot_0";
		spot.kind = ItemPlacementSpotKind::ShelfSlot;
		spot.position = { 3.0f, 1.15f, -1.5f };
		spot.rotationDeg = { 0.0f, 30.0f, 0.0f };
		spot.snapRadius = 0.55f;
		spot.allowPlayerDrop = true;
		spot.allowCustomerPlace = false;
		itemPlacementSpots.push_back(spot);
	}
}
void Game::ClearScenePropPlacementSpot(int propIndex)
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return;

	SceneProp& prop = sceneProps[propIndex];

	// Robust cleanup:
	// Clear every placement spot that points to this prop,
	// not only prop.currentPlacementSpotIndex.
	for (ItemPlacementSpot& spot : itemPlacementSpots)
	{
		if (spot.occupiedScenePropIndex == propIndex)
		{
			spot.occupiedScenePropIndex = -1;
		}
	}

	prop.currentPlacementSpotIndex = -1;
}

bool Game::IsPlacementSpotValidForProp(
	int propIndex,
	int spotIndex
) const
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return false;

	if (spotIndex < 0 || spotIndex >= (int)itemPlacementSpots.size())
		return false;

	const SceneProp& prop = sceneProps[propIndex];
	const ItemPlacementSpot& spot = itemPlacementSpots[spotIndex];

	if (!spot.enabled)
		return false;

	if (!spot.allowPlayerDrop)
		return false;

	if (IsPlacementSpotOccupiedByValidProp(spotIndex, propIndex))
	{
		return false;
	}

	bool isCounterSpot =
		spot.kind == ItemPlacementSpotKind::CounterOffer ||
		spot.kind == ItemPlacementSpotKind::CounterScan;

	// Counter accepts all objects.
	// Shelf slots can restrict by acceptedItemTag.
	if (!isCounterSpot &&
		!spot.acceptedItemTag.empty() &&
		spot.acceptedItemTag != prop.itemTag)
	{
		return false;
	}

	return true;
}

int Game::FindBestPlacementSpotForPropFromRay(
	int propIndex,
	Ray ray,
	float maxDistance
) const
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return -1;

	const SceneProp& prop = sceneProps[propIndex];

	Vector3 rayOrigin = ray.position;
	Vector3 rayDir = NormalizeSafe(ray.direction);

	int bestIndex = -1;
	float bestScore = FLT_MAX;

	for (int i = 0; i < (int)itemPlacementSpots.size(); i++)
	{
		if (!IsPlacementSpotValidForProp(propIndex, i))
			continue;

		const ItemPlacementSpot& spot = itemPlacementSpots[i];

		Vector3 toSpot = Vector3Subtract(spot.position, rayOrigin);

		float alongRay = Vector3DotProduct(toSpot, rayDir);

		if (alongRay < 0.0f || alongRay > maxDistance)
			continue;

		Vector3 closestPointOnRay = Vector3Add(
			rayOrigin,
			Vector3Scale(rayDir, alongRay)
		);

		float aimDistance = Vector3Distance(
			closestPointOnRay,
			spot.position
		);

		float aimRadius = fmaxf(spot.snapRadius, 0.20f);

		if (aimDistance > aimRadius)
			continue;

		// Aim accuracy should matter more than distance.
		// This helps when two shelf slots are stacked vertically.
		float score = aimDistance * 10.0f + alongRay * 0.05f;

		if (prop.preferHomePlacement && prop.homePlacementSpotIndex == i)
		{
			score -= 0.20f;
		}

		if (score < bestScore)
		{
			bestScore = score;
			bestIndex = i;
		}
	}

	return bestIndex;
}

void Game::ComputePlacementPreviewForSpot(
	int propIndex,
	int spotIndex,
	Vector3& outPosition,
	Vector3& outRotationDeg
) const
{
	outPosition = { 0.0f, 0.0f, 0.0f };
	outRotationDeg = { 0.0f, 0.0f, 0.0f };

	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return;

	if (spotIndex < 0 || spotIndex >= (int)itemPlacementSpots.size())
		return;

	const SceneProp& prop = sceneProps[propIndex];
	const ItemPlacementSpot& spot = itemPlacementSpots[spotIndex];

	outRotationDeg = GetScenePropRotationForPlacementSpot(
		prop,
		spot
	);

	outPosition = GetScenePropPositionForPlacementSpot(
		prop,
		spot,
		outRotationDeg
	);
}

void Game::UpdateHeldPlacementPreview()
{
	if (editMode) return;
	if (cursorUnlocked) return;
	if (inspectMode) return;
	if (!hasHeldBody) return;

	if (heldScenePropIndex < 0 ||
		heldScenePropIndex >= (int)sceneProps.size())
	{
		return;
	}

	targetedPlacementSpotIndex = -1;
	placementPreviewValid = false;
	placementPreviewPropIndex = -1;
	placementPreviewPosition = { 0.0f, 0.0f, 0.0f };
	placementPreviewRotationDeg = { 0.0f, 0.0f, 0.0f };

	SceneProp& prop = sceneProps[heldScenePropIndex];

	if (prop.model != nullptr)
	{
		BuildOutlineModelCache(prop.model);
	}

	Vector2 screenCenter = {
		(float)GetScreenWidth() * 0.5f,
		(float)GetScreenHeight() * 0.5f
	};

	Ray centerRay = GetScreenToWorldRay(screenCenter, camera);

	const float placementRayDistance = 3.0f;

	int spotIndex = FindBestPlacementSpotForPropFromRay(
		heldScenePropIndex,
		centerRay,
		placementRayDistance
	);

	// Fallback to old loose proximity logic if ray aim does not find a slot.
	if (spotIndex < 0)
	{
		const SceneProp& prop = sceneProps[heldScenePropIndex];

		Vector3 nearPosition = prop.position;

		if (physics && !prop.bodyId.IsInvalid())
		{
			nearPosition = physics->GetBodyPosition(prop.bodyId);
		}

		spotIndex = FindBestPlacementSpotForProp(
			heldScenePropIndex,
			nearPosition,
			prop.preferHomePlacement
		);
	}

	if (spotIndex < 0)
		return;

	targetedPlacementSpotIndex = spotIndex;
	placementPreviewPropIndex = heldScenePropIndex;

	ComputePlacementPreviewForSpot(
		heldScenePropIndex,
		spotIndex,
		placementPreviewPosition,
		placementPreviewRotationDeg
	);

	placementPreviewValid = true;
}

int Game::FindBestPlacementSpotForProp(
	int propIndex,
	Vector3 nearPosition,
	bool preferHomeSpot
) const
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return -1;

	const SceneProp& prop = sceneProps[propIndex];

	int bestIndex = -1;
	float bestScore = FLT_MAX;

	for (int i = 0; i < (int)itemPlacementSpots.size(); i++)
	{
		const ItemPlacementSpot& spot = itemPlacementSpots[i];

		if (!IsPlacementSpotValidForProp(propIndex, i))
			continue;

		float itemDist = Vector3Distance(nearPosition, spot.position);
		float playerDist = Vector3Distance(player.m_pos, spot.position);

		float score = fminf(itemDist, playerDist * 0.85f);

		if (score > spot.snapRadius)
			continue;

		if (preferHomeSpot && prop.homePlacementSpotIndex == i)
		{
			score -= 0.25f;
		}

		if (score < bestScore)
		{
			bestScore = score;
			bestIndex = i;
		}
	}

	return bestIndex;
}

int Game::FindItemPlacementSpotById(const std::string& id) const
{
	for (int i = 0; i < (int)itemPlacementSpots.size(); i++)
	{
		if (itemPlacementSpots[i].id == id)
			return i;
	}

	return -1;
}

int Game::FindFirstFreePlacementSpot(
	ItemPlacementSpotKind kind,
	bool forCustomer
) const
{
	for (int i = 0; i < (int)itemPlacementSpots.size(); i++)
	{
		const ItemPlacementSpot& spot = itemPlacementSpots[i];

		if (!spot.enabled)
			continue;

		if (spot.kind != kind)
			continue;

		if (forCustomer && !spot.allowCustomerPlace)
			continue;

		if (!forCustomer && !spot.allowPlayerDrop)
			continue;

		if (IsPlacementSpotOccupiedByValidProp(i))
			continue;

		return i;
	}

	return -1;
}

int Game::CreateCounterOfferItemForCustomer(int customerIndex)
{
	return CreateTradeItemScenePropForCustomer(
		customerIndex,
		ItemPlacementSpotKind::CounterOffer
	);
}

void Game::DrawItemPlacementSpotHierarchySection()
{
	if (!ImGui::TreeNodeEx("Item Placement Spots", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	if (ImGui::Button("Add Counter Offer Spot"))
	{
		PushUndoSnapshot();

		ItemPlacementSpot spot{};
		spot.id = TextFormat("counter_offer_%i", (int)itemPlacementSpots.size());
		spot.kind = ItemPlacementSpotKind::CounterOffer;
		spot.position = player.m_pos;
		spot.position.y += 1.0f;
		spot.rotationDeg = { 0.0f, player.yaw * RAD2DEG, 0.0f };
		spot.snapRadius = 0.65f;
		spot.allowPlayerDrop = true;
		spot.allowCustomerPlace = true;

		itemPlacementSpots.push_back(spot);
		selectedItemPlacementSpotIndex = (int)itemPlacementSpots.size() - 1;

		selectedScenePropIndex = -1;
		selectedInstancedPropIndex = -1;
		selectedCustomerIndex = -1;
	}

	ImGui::SameLine();

	if (ImGui::Button("Add Shelf Slot"))
	{
		PushUndoSnapshot();

		ItemPlacementSpot spot{};
		spot.id = TextFormat("shelf_slot_%i", (int)itemPlacementSpots.size());
		spot.kind = ItemPlacementSpotKind::ShelfSlot;
		spot.position = player.m_pos;
		spot.position.y += 1.0f;
		spot.rotationDeg = { 0.0f, player.yaw * RAD2DEG, 0.0f };
		spot.snapRadius = 0.55f;
		spot.allowPlayerDrop = true;
		spot.allowCustomerPlace = false;

		itemPlacementSpots.push_back(spot);
		selectedItemPlacementSpotIndex = (int)itemPlacementSpots.size() - 1;

		selectedScenePropIndex = -1;
		selectedInstancedPropIndex = -1;
		selectedCustomerIndex = -1;
	}

	ImGui::Separator();

	for (int i = 0; i < (int)itemPlacementSpots.size(); i++)
	{
		ItemPlacementSpot& spot = itemPlacementSpots[i];

		ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_Leaf |
			ImGuiTreeNodeFlags_NoTreePushOnOpen |
			ImGuiTreeNodeFlags_SpanAvailWidth;

		if (selectedItemPlacementSpotIndex == i)
			flags |= ImGuiTreeNodeFlags_Selected;

		std::string label = spot.id;

		if (spot.occupiedScenePropIndex >= 0)
			label += " [occupied]";

		ImGui::TreeNodeEx(
			(void*)(intptr_t)(300000 + i),
			flags,
			"%s",
			label.c_str()
		);

		if (ImGui::IsItemClicked())
		{
			selectedItemPlacementSpotIndex = i;

			selectedScenePropIndex = -1;
			selectedInstancedPropIndex = -1;
			selectedCustomerIndex = -1;
			selectedEditorItemIndex = -1;
		}
	}

	ImGui::TreePop();
}

bool Game::IsPlacementSpotOccupiedByValidProp(
	int spotIndex,
	int allowedPropIndex
) const
{
	if (spotIndex < 0 || spotIndex >= (int)itemPlacementSpots.size())
		return false;

	const ItemPlacementSpot& spot = itemPlacementSpots[spotIndex];

	int occupiedPropIndex = spot.occupiedScenePropIndex;

	if (occupiedPropIndex < 0)
		return false;

	if (occupiedPropIndex == allowedPropIndex)
		return false;

	if (occupiedPropIndex >= (int)sceneProps.size())
		return false;

	const SceneProp& occupiedProp = sceneProps[occupiedPropIndex];

	// Important: protect against stale spot occupancy.
	if (occupiedProp.currentPlacementSpotIndex != spotIndex)
		return false;

	return true;
}

int Game::CreateTradeItemScenePropForCustomer(
	int customerIndex,
	ItemPlacementSpotKind spotKind
)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return -1;

	Customer& customer = customers[customerIndex];

	if (customer.tradeItemId.empty())
	{
		AssignTradeItemToCustomer(
			customerIndex,
			customer.role == CustomerRole::Seller
		);
	}

	const CustomerTradeItemDef* item = FindCustomerTradeItemDef(
		customer.tradeItemId
	);

	if (item == nullptr)
	{
		TraceLog(
			LOG_WARNING,
			"CreateTradeItemScenePropForCustomer failed: item not found '%s'.",
			customer.tradeItemId.c_str()
		);

		return -1;
	}

	int spotIndex = FindFirstFreePlacementSpot(
		spotKind,
		true
	);

	if (spotIndex < 0)
	{
		TraceLog(
			LOG_WARNING,
			"No free placement spot for trade item kind=%i.",
			(int)spotKind
		);

		return -1;
	}

	const ItemPlacementSpot& spot = itemPlacementSpots[spotIndex];

	bool sellerCustomer = customer.role == CustomerRole::Seller;

	int templatePropIndex = FindTradeItemTemplateSceneProp(
		item->id,
		sellerCustomer
	);

	const SceneProp* templateProp =
		templatePropIndex >= 0
		? &sceneProps[templatePropIndex]
		: nullptr;

	std::string propName = "Customer Item - " + item->displayName;

	if (templateProp != nullptr)
	{
		SceneProp newProp = *templateProp;

		newProp.name = propName;

		newProp.position = spot.position;
		newProp.rotationDeg = spot.rotationDeg;

		newProp.visible = true;
		newProp.canPickup = true;
		newProp.placedByCustomer = true;

		newProp.currentPlacementSpotIndex = -1;
		newProp.homePlacementSpotIndex = -1;

		newProp.parentIndex = -1;
		newProp.childIndices.clear();

		newProp.bodyId = JPH::BodyID();

		newProp.simulatePhysics = true;
		newProp.syncFromPhysics = false;
		newProp.editLockPhysics = true;

		newProp.hasCollision = true;
		newProp.blocksPlayer = true;
		newProp.useJoltCollider = true;
		newProp.useNormalCollision = false;

		newProp.tradeItemId = item->id;

		if (newProp.itemTag.empty())
			newProp.itemTag = item->itemTag;

		if (newProp.inspectDialogueTag.empty())
			newProp.inspectDialogueTag = item->id;

		if (newProp.inspectDialogueLines.empty())
			newProp.inspectDialogueLines = item->inspectLines;

		sceneProps.push_back(newProp);
	}
	else
	{
		Model* model = item->model != nullptr
			? item->model
			: &gBoy;

		AddSceneProp(
			propName,
			model,
			spot.position,
			spot.rotationDeg,
			item->scale,
			true,
			{ 0.0f, 0.0f, 0.0f },
			{ 0.0f, 0.0f, 0.0f },
			true,
			false,
			WHITE
		);
	}

	int propIndex = (int)sceneProps.size() - 1;
	SceneProp& prop = sceneProps[propIndex];

	RefreshScenePropRenderBounds(prop);

	prop.canPickup = true;
	prop.placedByCustomer = true;
	prop.pickupCategory = PickupItemCategory::Generic;
	prop.tradeItemId = item->id;

	prop.owningCustomerIndex = customerIndex;
	prop.scannedForCustomer = false;
	prop.scannedPriceYen = 0;

	if (prop.itemTag.empty() || prop.itemTag == "generic")
		prop.itemTag = item->itemTag;

	if (prop.inspectDialogueTag.empty())
		prop.inspectDialogueTag = item->id;

	if (prop.inspectDialogueLines.empty())
		prop.inspectDialogueLines = item->inspectLines;

	prop.hasCollision = true;
	prop.blocksPlayer = true;
	prop.useJoltCollider = true;
	prop.useNormalCollision = false;

	prop.simulatePhysics = true;
	prop.syncFromPhysics = false;
	prop.editLockPhysics = true;

	if (templateProp == nullptr)
	{
		prop.holdOffsetLocal = item->holdOffsetLocal;
		prop.holdRotationOffsetDeg = item->holdRotationOffsetDeg;
		prop.holdFollowCameraPitch = item->holdFollowCameraPitch;

		prop.snapUprightOnDrop = true;
		prop.dropRotationOffsetDeg = item->dropRotationOffsetDeg;
	}

	MarkShadowTextureBindingsDirty(false);

	if (!PlaceScenePropAtSpot(propIndex, spotIndex, true))
	{
		TraceLog(
			LOG_WARNING,
			"Failed to place customer trade item at counter spot."
		);

		return propIndex;
	}

	customer.counterItemScenePropIndex = propIndex;
	customer.counterItemPlaced = true;

	if (customer.role == CustomerRole::Browser)
	{
		customer.waitingForPlayerToScanItem = true;
		customer.waitingForPlayerToReturnScannedItem = false;
	}

	UpdateScenePropWorldTransforms();
	RecreatePhysicsWorld();

	TraceLog(
		LOG_INFO,
		"Customer %i placed item '%s' at spot '%s'.",
		customerIndex,
		item->id.c_str(),
		spot.id.c_str()
	);

	return propIndex;
}
bool Game::EnsureCustomerCounterItemPlaced(int customerIndex)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return false;

	Customer& customer = customers[customerIndex];

	if (customer.counterItemPlaced)
		return false;

	if (customer.counterItemPlacementAttempted)
		return false;

	ItemPlacementSpotKind spotKind = ItemPlacementSpotKind::CounterOffer;

	if (customer.role == CustomerRole::Seller)
	{
		AssignTradeItemToCustomer(customerIndex, true);
		spotKind = ItemPlacementSpotKind::CounterOffer;
	}
	else if (customer.role == CustomerRole::Browser)
	{
		if (!customer.hasPickedItem)
			return false;

		if (customer.tradeItemId.empty())
		{
			AssignTradeItemToCustomer(customerIndex, false);
		}

		spotKind = ItemPlacementSpotKind::CounterOffer;
	}
	else
	{
		return false;
	}

	int propIndex = CreateTradeItemScenePropForCustomer(
		customerIndex,
		spotKind
	);

	if (propIndex < 0)
	{
		TraceLog(
			LOG_WARNING,
			"Customer %i tried to place trade item '%s', but placement failed.",
			customerIndex,
			customer.tradeItemId.c_str()
		);

		return false;
	}

	customer.counterItemPlacementAttempted = true;
	customer.poiWaitTimer = 1.05f;

	return true;
}


void Game::RebuildItemPlacementSpotOccupancy()
{
	for (ItemPlacementSpot& spot : itemPlacementSpots)
	{
		spot.occupiedScenePropIndex = -1;
	}

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		SceneProp& prop = sceneProps[i];

		if (prop.currentPlacementSpotIndex < 0 ||
			prop.currentPlacementSpotIndex >= (int)itemPlacementSpots.size())
		{
			prop.currentPlacementSpotIndex = -1;
			continue;
		}

		ItemPlacementSpot& spot = itemPlacementSpots[prop.currentPlacementSpotIndex];

		if (spot.occupiedScenePropIndex < 0)
		{
			spot.occupiedScenePropIndex = i;
		}
		else if (spot.occupiedScenePropIndex != i)
		{
			prop.currentPlacementSpotIndex = -1;
		}
	}
}

bool Game::PlaceScenePropAtSpot(
	int propIndex,
	int spotIndex,
	bool lockPhysics
)
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return false;

	if (spotIndex < 0 || spotIndex >= (int)itemPlacementSpots.size())
		return false;

	SceneProp& prop = sceneProps[propIndex];
	ItemPlacementSpot& spot = itemPlacementSpots[spotIndex];

	if (!spot.enabled)
		return false;

	if (IsPlacementSpotOccupiedByValidProp(spotIndex, propIndex))
	{
		return false;
	}

	ClearScenePropPlacementSpot(propIndex);
	Vector3 placementRotation = GetScenePropRotationForPlacementSpot(
		prop,
		spot
	);

	prop.rotationDeg = placementRotation;
	prop.position = GetScenePropPositionForPlacementSpot(
		prop,
		spot,
		placementRotation
	);

	prop.currentPlacementSpotIndex = spotIndex;

	if (prop.homePlacementSpotIndex < 0)
	{
		prop.homePlacementSpotIndex = spotIndex;
	}

	spot.occupiedScenePropIndex = propIndex;

	if (lockPhysics)
	{
		prop.simulatePhysics = true;
		prop.syncFromPhysics = false;
		prop.editLockPhysics = true;
	}

	if (prop.importedFromGlbScene && prop.model != nullptr)
	{
		SyncImportedEditorOffsetFromRuntime(prop);
	}
	else if (prop.parentIndex == -1)
	{
		SyncScenePropLocalFromWorld(prop);
	}

	if (physics && !prop.bodyId.IsInvalid())
	{
		ApplyScenePropTransformToBody(prop);

		if (lockPhysics)
		{
			physics->SetBodyMotionType(prop.bodyId, JPH::EMotionType::Kinematic);
			physics->SetBodyGravityFactor(prop.bodyId, 0.0f);
			physics->SetBodyLinearVelocity(prop.bodyId, { 0.0f, 0.0f, 0.0f });
			physics->SetBodyAngularVelocity(prop.bodyId, { 0.0f, 0.0f, 0.0f });
			physics->SetBodyIsSensor(prop.bodyId, false);
		}
	}

	customerNavGridDirty = true;
	MarkShadowMapsDirty();

	OnScenePropPlacedAtSpot(propIndex, spotIndex);

	return true;
}
bool Game::TrySnapHeldPropToPlacementSpot(int propIndex)
{
	if (propIndex < 0 || propIndex >= (int)sceneProps.size())
		return false;

	SceneProp& prop = sceneProps[propIndex];

	// First priority: currently aimed placement preview.
	if (placementPreviewValid &&
		placementPreviewPropIndex == propIndex &&
		targetedPlacementSpotIndex >= 0)
	{
		// Scanner is not a real placement slot.
		// It is handled before StopHoldingBody().
		if (IsScannerSpot(targetedPlacementSpotIndex))
			return false;

		return PlaceScenePropAtSpot(
			propIndex,
			targetedPlacementSpotIndex,
			true
		);
	}

	// Second priority: current screen-center ray.
	Vector2 screenCenter = {
		(float)GetScreenWidth() * 0.5f,
		(float)GetScreenHeight() * 0.5f
	};

	Ray centerRay = GetScreenToWorldRay(screenCenter, camera);

	int spotIndex = FindBestPlacementSpotForPropFromRay(
		propIndex,
		centerRay,
		3.0f
	);

	// Final fallback: old proximity logic.
	if (spotIndex < 0)
	{
		Vector3 nearPosition = prop.position;

		if (physics && !prop.bodyId.IsInvalid())
		{
			nearPosition = physics->GetBodyPosition(prop.bodyId);
		}

		spotIndex = FindBestPlacementSpotForProp(
			propIndex,
			nearPosition,
			prop.preferHomePlacement
		);
	}

	if (spotIndex < 0)
		return false;

	// Scanner should never store the item as a normal placement.
	if (IsScannerSpot(spotIndex))
		return false;

	return PlaceScenePropAtSpot(propIndex, spotIndex, true);
}
void Game::DrawHeldPlacementPreview() const
{
	if (!placementPreviewValid)
		return;

	if (!hasHeldBody)
		return;

	if (placementPreviewPropIndex < 0 ||
		placementPreviewPropIndex >= (int)sceneProps.size())
	{
		return;
	}

	const SceneProp& prop = sceneProps[placementPreviewPropIndex];

	if (prop.model == nullptr)
		return;

	const OutlineModelCache* cache = GetOutlineModelCache(prop.model);
	if (cache == nullptr)
		return;

	Matrix previewTransform = GetScenePropPreviewDrawMatrix(
		prop,
		placementPreviewPosition,
		placementPreviewRotationDeg
	);

	DrawScenePropOutlineWithMatrix(
		placementPreviewPropIndex,
		previewTransform,
		camera,
		false
	);
}


void Game::UpdateCustomersSafely(float dt)
{
	UpdateCustomersWithCollision(dt);
}
int Game::CountActiveCustomers() const
{
	int count = 0;

	for (const Customer& customer : customers)
	{
		if (!customer.pendingDespawn)
			count++;
	}

	return count;
}

bool Game::CustomerWouldHitInstancedProps(Vector3 position) const
{
	BoundingBox customerBox = MakeCustomerCollisionBoxAt(position);

	for (const InstancedProp& prop : instancedProps)
	{
		if (!prop.visible)
			continue;

		if (!prop.hasCollision)
			continue;

		if (!prop.blocksPlayer)
			continue;

		if (prop.type != InstancePropType::GachaMachine)
			continue;

		Vector3 scaledSize = {
			fabsf(prop.colliderSize.x * prop.scale.x),
			fabsf(prop.colliderSize.y * prop.scale.y),
			fabsf(prop.colliderSize.z * prop.scale.z)
		};

		const float navPadding = 0.0f;

		Vector3 half = {
			scaledSize.x * 0.5f + navPadding,
			scaledSize.y * 0.5f,
			scaledSize.z * 0.5f + navPadding
		};

		Quaternion q = QuaternionFromEuler(
			prop.rotationDeg.x * DEG2RAD,
			prop.rotationDeg.y * DEG2RAD,
			prop.rotationDeg.z * DEG2RAD
		);

		Vector3 scaledOffset = {
			prop.colliderOffset.x * prop.scale.x,
			prop.colliderOffset.y * prop.scale.y,
			prop.colliderOffset.z * prop.scale.z
		};

		Vector3 rotatedOffset = Vector3RotateByQuaternion(scaledOffset, q);
		Vector3 center = Vector3Add(prop.position, rotatedOffset);

		BoundingBox box{};
		box.min = {
			center.x - half.x,
			center.y - half.y,
			center.z - half.z
		};

		box.max = {
			center.x + half.x,
			center.y + half.y,
			center.z + half.z
		};

		if (CheckCollisionBoxes(customerBox, box))
			return true;
	}

	return false;
}
void Game::AddCustomerBodyForCustomer(int customerIndex)
{
	if (!physics)
		return;

	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	if ((int)customerBodyIds.size() < (int)customers.size())
	{
		customerBodyIds.resize(customers.size());
	}

	if (!customerBodyIds[customerIndex].IsInvalid())
		return;

	Customer& customer = customers[customerIndex];

	Vector3 center = GetCustomerColliderCenter(customer);

	Vector3 halfExtents = {
		0.225f,
		0.875f,
		0.225f
	};

	Quaternion rotation = GetCustomerColliderRotation(customer);

	JPH::BodyID bodyId = physics->AddKinematicBox(
		center,
		halfExtents,
		rotation
	);

	customerBodyIds[customerIndex] = bodyId;
}

void Game::FreezeActiveDialogueCustomer()
{
	if (!dialogueActive)
		return;

	if (activeDialogueCustomerIndex < 0 ||
		activeDialogueCustomerIndex >= (int)customers.size())
		return;

	Customer& customer = customers[activeDialogueCustomerIndex];

	// Stop movement/pathing only.
	// Do NOT call SetAnimState(Idle) here, because dialogue cues
	// may be playing one-shot animations such as Think, Give, Point, etc.
	customer.hasMoveTarget = false;
	customer.pathWaypoints.clear();
	customer.pathWaypointCursor = 0;
	customer.movementPauseTimer = 0.0f;

	SetCustomerYawTowards(activeDialogueCustomerIndex, player.m_pos);
}

void Game::ProcessPendingCustomerBodyCreates()
{
	if (!physics)
		return;

	if ((int)customerBodyIds.size() < (int)customers.size())
	{
		customerBodyIds.resize(customers.size());
	}

	int bodiesCreatedThisFrame = 0;
	const int maxBodiesCreatedThisFrame = 1;

	for (int i = 0; i < (int)customers.size(); i++)
	{
		if (customers[i].pendingDespawn)
			continue;

		if (i >= (int)customerBodyIds.size())
			continue;

		if (!customerBodyIds[i].IsInvalid())
			continue;

		AddCustomerBodyForCustomer(i);

		bodiesCreatedThisFrame++;

		if (bodiesCreatedThisFrame >= maxBodiesCreatedThisFrame)
			break;
	}
}

int Game::CountActiveCustomersOfType(const std::string& typeId) const
{
	int count = 0;

	for (const Customer& customer : customers)
	{
		if (customer.pendingDespawn)
			continue;

		if (customer.customerTypeId == typeId)
			count++;
	}

	return count;

}

int Game::FindPOIByKind(Game::CustomerPOIKind kind) const
{
	for (int i = 0; i < (int)customerPOIs.size(); i++)
	{
		const CustomerPOI& poi = customerPOIs[i];

		if (!poi.enabled)
			continue;

		if (poi.kind == kind)
			return i;
	}

	return -1;
}
void Game::UpdateCustomerSpawner(float dt)
{

	if (storeDayState != StoreDayState::Open)
		return;

	if (customersSpawnedToday >= customersToProcessBeforeClose)
	{
		BeginStoreClosing("Daily customer limit reached.");
		return;
	}

	if (!customerSpawningEnabled)
		return;

	if (CountActiveCustomers() >= maxCustomerCount)
		return;

	customerSpawnTimer += dt;

	if (customerSpawnTimer < nextCustomerSpawnDelay)
		return;

	bool spawned = SpawnRandomCustomer();

	if (spawned)
	{
		customersSpawnedToday++;

		customerSpawnTimer = 0.0f;

		nextCustomerSpawnDelay = GetCurrentDaySpawnDelay();

		if (customersSpawnedToday >= customersToProcessBeforeClose)
		{
			BeginStoreClosing("Daily customer limit reached.");
		}
	}
	else
	{
		// Failed attempt: retry soon, do not wait the full 4-8 seconds.
		customerSpawnTimer = 0.0f;
		nextCustomerSpawnDelay = 0.75f;
	}
}
bool Game::SpawnRandomCustomer()
{
	int entry = FindPOIByKind(CustomerPOIKind::Entry);

	if (entry < 0)
	{
		TraceLog(LOG_WARNING, "SpawnRandomCustomer failed: no Entry POI.");
		return false;
	}

	Vector3 spawnPosition{};

	if (customerNavGridDirty || !customerNavGrid.valid)
	{
		RebuildCustomerNavGrid();
	}

	if (!FindCustomerSpawnPositionNearPOI(entry, spawnPosition))
	{
		TraceLog(
			LOG_WARNING,
			"SpawnRandomCustomer failed: no free spawn position near Entry POI. Entry pos=(%.2f %.2f %.2f)",
			customerPOIs[entry].position.x,
			customerPOIs[entry].position.y,
			customerPOIs[entry].position.z
		);

		return false;
	}

	std::vector<std::string> modelTypes = {
		"browser_male",
		"seller_male",
		"new_customer"
	};

	int lowestModelCount = INT_MAX;
	std::vector<std::string> leastUsedModelTypes;

	for (const std::string& modelTypeId : modelTypes)
	{
		int count = CountActiveCustomersOfType(modelTypeId);

		if (count < lowestModelCount)
		{
			lowestModelCount = count;
			leastUsedModelTypes.clear();
			leastUsedModelTypes.push_back(modelTypeId);
		}
		else if (count == lowestModelCount)
		{
			leastUsedModelTypes.push_back(modelTypeId);
		}
	}

	if (leastUsedModelTypes.empty())
	{
		TraceLog(LOG_WARNING, "SpawnRandomCustomer failed: no model types available.");
		return false;
	}

	std::string typeId = leastUsedModelTypes[
		GetRandomValue(0, (int)leastUsedModelTypes.size() - 1)
	];

	CustomerRole spawnRole = CustomerRole::Browser;

	int browserCount = CountActiveCustomersByRole(CustomerRole::Browser);
	int sellerCount = CountActiveCustomersByRole(CustomerRole::Seller);

	if (browserCount <= 0)
	{
		spawnRole = CustomerRole::Browser;
	}
	else if (sellerCount <= 0)
	{
		spawnRole = CustomerRole::Seller;
	}
	else
	{
		StoreDayConfig cfg = GetCurrentStoreDayConfig();

		int roleRoll = GetRandomValue(0, 99);

		spawnRole = roleRoll < cfg.sellerChancePercent
			? CustomerRole::Seller
			: CustomerRole::Browser;
	}

	Customer* customer = SpawnCustomerOfType(
		typeId,
		spawnPosition,
		CustomerAnimState::Idle
	);

	if (customer == nullptr)
	{
		TraceLog(
			LOG_WARNING,
			"SpawnRandomCustomer failed: SpawnCustomerOfType returned nullptr for type=%s",
			typeId.c_str()
		);

		return false;
	}

	customer->role = spawnRole;

	if (spawnRole == CustomerRole::Seller)
	{
		customer->poiGroup = "seller";
		customer->dialogueScriptId = "seller_part1";
	}
	else
	{
		customer->poiGroup = "browser";
		customer->dialogueScriptId = "browser_customer";
	}

	customer->aiState = CustomerAIState::Entering;
	customer->currentPOIIndex = entry;
	customer->targetPOIIndex = -1;
	customer->destinationPOIIndex = -1;
	customer->assignedItemPOIIndex = -1;
	customer->assignedQueueSlotIndex = -1;

	customer->hasMoveTarget = false;
	customer->pathWaypoints.clear();
	customer->pathWaypointCursor = 0;

	customer->movementPauseTimer = 0.0f;
	customer->poiWaitTimer = 0.05f;

	if (customer->role == CustomerRole::Seller)
	{
		customer->aiState = CustomerAIState::SellerGoingToQueue;
	}
	else
	{
		customer->browseVisitsRemaining = GetRandomValue(3, 6);
		customer->aiState = CustomerAIState::BrowserBrowsing;
	}

	if ((int)customerBodyIds.size() < (int)customers.size())
	{
		customerBodyIds.resize(customers.size());
	}

	// Keep the body invalid for now.
	// ProcessPendingCustomerBodyCreates() will create it on a later frame.
	customerBodyIds[(int)customers.size() - 1] = JPH::BodyID();

	// Give the customer a tiny settle delay so spawn, body creation,
	// and first route do not all happen on the same frame.
	customer->movementPauseTimer = 0.08f;
	customer->poiWaitTimer = 0.08f;

	TraceLog(
		LOG_INFO,
		"Spawned customer: index=%i modelType=%s role=%s dialogue=%s pos=(%.2f %.2f %.2f)",
		(int)customers.size() - 1,
		customer->customerTypeId.c_str(),
		customer->role == CustomerRole::Seller ? "Seller" : "Browser",
		customer->dialogueScriptId.c_str(),
		spawnPosition.x,
		spawnPosition.y,
		spawnPosition.z
	);

	return true;
}
std::vector<Game::InstancedPropState> Game::CaptureInstancedPropStates() const
{
	std::vector<InstancedPropState> states;
	states.reserve(instancedProps.size());

	for (const InstancedProp& prop : instancedProps)
	{
		InstancedPropState s{};

		s.name = prop.name;
		s.type = (int)prop.type;

		s.position = prop.position;
		s.rotationDeg = prop.rotationDeg;
		s.scale = prop.scale;

		s.visible = prop.visible;

		states.push_back(s);
	}

	return states;
}

void Game::BuildInstancedBatchAlphaSettings(InstancedModelBatch& batch)
{
	if (!batch.loaded)
		return;

	const Model& model = batch.model;

	batch.meshAlphaModes.assign(model.meshCount, 0);
	batch.meshAlphaCutoffs.assign(model.meshCount, 0.5f);
	batch.meshBaseAlphas.assign(model.meshCount, 1.0f);
	batch.meshReflectionStrengths.assign(model.meshCount, 0.025f);

	for (int meshIndex = 0; meshIndex < model.meshCount; meshIndex++)
	{
		int matIndex = 0;

		if (model.meshMaterial != nullptr)
			matIndex = model.meshMaterial[meshIndex];

		if (matIndex < 0 || matIndex >= model.materialCount)
			matIndex = 0;

		const Material& mat = model.materials[matIndex];

		float baseAlpha =
			(float)mat.maps[MATERIAL_MAP_ALBEDO].color.a / 255.0f;

		int alphaMode = 0;
		float reflectionStrength = 0.025f;

		// Automatic fallback: if Blender exported material alpha
		if (baseAlpha < 0.98f)
		{
			alphaMode = 2;
			reflectionStrength = 0.08f;
		}

		// Manual override by material index
		if (ContainsMaterialIndex(batch.transparentMaterialIndices, matIndex))
		{
			alphaMode = 2;

			if (baseAlpha >= 0.98f)
				baseAlpha = 0.35f;

			reflectionStrength = 0.08f;
		}

		batch.meshAlphaModes[meshIndex] = alphaMode;
		batch.meshAlphaCutoffs[meshIndex] = 0.5f;
		batch.meshBaseAlphas[meshIndex] = baseAlpha;
		batch.meshReflectionStrengths[meshIndex] = reflectionStrength;
	}
}

void Game::ApplyInstancedPropStates(const std::vector<InstancedPropState>& states)
{
	instancedProps.clear();
	instancedProps.reserve(states.size());

	for (const InstancedPropState& s : states)
	{
		InstancedProp prop{};

		prop.name = s.name;
		prop.type = (InstancePropType)s.type;

		prop.position = s.position;
		prop.rotationDeg = s.rotationDeg;
		prop.scale = s.scale;

		prop.visible = s.visible;

		prop.hasCollision = true;
		prop.blocksPlayer = true;
		prop.autoCollider = true;

		if (prop.type == InstancePropType::GachaMachine && gachaInstanceColliderReady)
		{
			prop.colliderSize = gachaInstanceColliderSize;
			prop.colliderOffset = gachaInstanceColliderOffset;
		}
		else if (prop.type == InstancePropType::Basket && basketInstanceColliderReady)
		{
			prop.colliderSize = basketInstanceColliderSize;
			prop.colliderOffset = basketInstanceColliderOffset;
		}

		instancedProps.push_back(prop);
	}

	selectedInstancedPropIndex = -1;
	instancedPropsDirty = true;

	RebuildInstancedPropTransforms();
	if (physics)
	{
		BuildStaticBodiesFromInstancedProps();
	}

	customerNavGridDirty = true;

	MarkShadowMapsDirty();
}

void Game::RebuildInstancedPropTransforms()
{
	gachaInstanceBatch.transforms.clear();
	basketInstanceBatch.transforms.clear();

	for (const InstancedProp& prop : instancedProps)
	{
		if (!prop.visible)
			continue;

		Matrix world = MakeInstanceTRS(
			prop.position,
			prop.rotationDeg,
			prop.scale
		);

		switch (prop.type)
		{
		case InstancePropType::GachaMachine:
			gachaInstanceBatch.transforms.push_back(world);
			break;

		case InstancePropType::Basket:
			basketInstanceBatch.transforms.push_back(world);
			break;
		}
	}

	BuildGachaBallWorldTransforms();
	BuildBasketCartridgeWorldTransforms();

	instancedPropsDirty = false;
}

void Game::LoadGachaMachineInstanceTest()
{
	if (instancedPbrShader.id == 0)
	{
		TraceLog(LOG_WARNING, "instancedPbrShader is not loaded before LoadGachaMachineInstanceTest().");
		return;
	}

	gachaInstanceBatch.model = LoadModel("Models/Gachamachine_noBalls3.glb");
	gachaInstanceBatch.loaded = gachaInstanceBatch.model.meshCount > 0;

	if (!gachaInstanceBatch.loaded)
	{
		TraceLog(LOG_WARNING, "Failed to load Models/Gachamachine_noBalls.glb for instancing.");
		return;
	}

	for (int i = 0; i < gachaInstanceBatch.model.materialCount; i++)
	{
		gachaInstanceBatch.model.materials[i].shader = instancedPbrShader;
	}
	ComputeGachaInstanceAutoCollider();
	BuildInstancedBatchAlphaSettings(gachaInstanceBatch);


	TraceLog(
		LOG_INFO,
		"Gacha instancing model loaded: meshes=%i materials=%i instances=%i",
		gachaInstanceBatch.model.meshCount,
		gachaInstanceBatch.model.materialCount,
		(int)gachaInstanceBatch.transforms.size()
	);
}
void Game::CreateDefaultGachaInstanceProps()
{
	instancedProps.clear();

	for (int i = 0; i < 10; i++)
	{
		InstancedProp prop{};

		prop.name = TextFormat("GachaMachine_%02i", i);
		prop.type = InstancePropType::GachaMachine;

		prop.position = {
			-4.0f + i * 0.85f,
			0.0f,
			2.0f
		};

		prop.rotationDeg = { 0.0f, 0.0f, 0.0f };
		prop.scale = { 1.0f, 1.0f, 1.0f };
		prop.visible = true;

		prop.hasCollision = true;
		prop.blocksPlayer = true;
		prop.autoCollider = true;

		if (gachaInstanceColliderReady)
		{
			prop.colliderSize = gachaInstanceColliderSize;
			prop.colliderOffset = gachaInstanceColliderOffset;
		}


		instancedProps.push_back(prop);
	}

	instancedPropsDirty = true;
}
void Game::RemoveInstancedPropCollider(int index)
{
	if (!physics)
		return;

	if (index < 0 || index >= (int)instancedProps.size())
		return;

	InstancedProp& prop = instancedProps[index];

	if (!prop.bodyId.IsInvalid())
	{
		physics->RemoveBody(prop.bodyId);
		prop.bodyId = JPH::BodyID();
	}
}
void Game::RebuildInstancedPropCollider(int index)
{
	if (!physics)
		return;

	if (index < 0 || index >= (int)instancedProps.size())
		return;

	InstancedProp& prop = instancedProps[index];

	// Remove old static body first.
	RemoveInstancedPropCollider(index);

	if (!prop.visible)
		return;

	if (!prop.hasCollision || !prop.blocksPlayer)
		return;

	if (prop.type == InstancePropType::GachaMachine)
	{
		if (prop.autoCollider && gachaInstanceColliderReady)
		{
			prop.colliderSize = gachaInstanceColliderSize;
			prop.colliderOffset = gachaInstanceColliderOffset;
		}
	}
	else if (prop.type == InstancePropType::Basket)
	{
		if (prop.autoCollider && basketInstanceColliderReady)
		{
			prop.colliderSize = basketInstanceColliderSize;
			prop.colliderOffset = basketInstanceColliderOffset;
		}
	}
	else
	{
		return;
	}

	Vector3 scaledSize = {
		fabsf(prop.colliderSize.x * prop.scale.x),
		fabsf(prop.colliderSize.y * prop.scale.y),
		fabsf(prop.colliderSize.z * prop.scale.z)
	};

	Vector3 halfExtents = {
		scaledSize.x * 0.5f,
		scaledSize.y * 0.5f,
		scaledSize.z * 0.5f
	};

	if (!IsUsableJoltHalfExtents(halfExtents))
	{
		TraceLog(
			LOG_WARNING,
			"Skipped instanced collider for %s: invalid half extents %.3f %.3f %.3f",
			prop.name.c_str(),
			halfExtents.x,
			halfExtents.y,
			halfExtents.z
		);

		return;
	}

	Quaternion rot = QuaternionFromEuler(
		prop.rotationDeg.x * DEG2RAD,
		prop.rotationDeg.y * DEG2RAD,
		prop.rotationDeg.z * DEG2RAD
	);

	Vector3 scaledOffset = {
		prop.colliderOffset.x * prop.scale.x,
		prop.colliderOffset.y * prop.scale.y,
		prop.colliderOffset.z * prop.scale.z
	};

	Vector3 rotatedOffset = Vector3RotateByQuaternion(scaledOffset, rot);
	Vector3 center = Vector3Add(prop.position, rotatedOffset);

	prop.bodyId = physics->AddStaticBox(
		center,
		halfExtents,
		rot
	);
}
void Game::BuildStaticBodiesFromInstancedProps()
{
	if (!physics)
		return;

	for (InstancedProp& prop : instancedProps)
	{
		prop.bodyId = JPH::BodyID();
	}

	for (int i = 0; i < (int)instancedProps.size(); i++)
	{
		RebuildInstancedPropCollider(i);
	}

	TraceLog(
		LOG_INFO,
		"Built %i instanced prop static colliders.",
		(int)instancedProps.size()
	);
}

void Game::BuildVisibleGachaBallTransforms(const Camera3D& cam) const
{
	visibleGachaBallTransformsScratch.clear();

	if (gachaBallLocalTransforms.empty())
		return;

	if (gachaInstanceBatch.transforms.empty())
		return;

	const float machineCullRadius = 2.6f;
	const float maxBallDrawDistance = 12.0f;

	for (int machineIndex = 0; machineIndex < (int)gachaInstanceBatch.transforms.size(); machineIndex++)
	{
		const Matrix& machineWorld = gachaInstanceBatch.transforms[machineIndex];

		Vector3 machineCenter = GetMatrixTranslationSafe(machineWorld);

		float distToCamera = Vector3Distance(cam.position, machineCenter);

		if (distToCamera > maxBallDrawDistance)
			continue;

		if (!IsSphereInCameraView(
			cam,
			machineCenter,
			machineCullRadius,
			maxBallDrawDistance
		))
		{
			continue;
		}

		// Some machines are intentionally empty.
		// But if the player is very close, always show balls so the closest machine looks good.
		if (!ShouldGachaMachineHaveBalls(machineIndex))
			continue;

		for (const BallLocalTransform& ball : gachaBallLocalTransforms)
		{
			Matrix ballWorld = MatrixMultiply(ball.localMatrix, machineWorld);
			visibleGachaBallTransformsScratch.push_back(ballWorld);
		}
	}
}

void Game::PrepareInstancedPBRShader(const Camera3D& cam) const
{
	if (instancedPbrShader.id == 0)
		return;

	float cameraPos[3] = {
		cam.position.x,
		cam.position.y,
		cam.position.z
	};

	SetShaderValue(
		instancedPbrShader,
		instancedPbrShader.locs[SHADER_LOC_VECTOR_VIEW],
		cameraPos,
		SHADER_UNIFORM_VEC3
	);

	Vector2 oneTiling = { 1.0f, 1.0f };
	Vector2 zeroOffset = { 0.0f, 0.0f };

	SetShaderValueIfValid(
		instancedPbrShader,
		instancedTilingLoc,
		&oneTiling,
		SHADER_UNIFORM_VEC2
	);

	SetShaderValueIfValid(
		instancedPbrShader,
		instancedOffsetLoc,
		&zeroOffset,
		SHADER_UNIFORM_VEC2
	);
}

bool Game::InstancedBatchHasTransparentMeshes(const InstancedModelBatch& batch) const
{


	for (int alphaMode : batch.meshAlphaModes)
	{
		if (alphaMode == 2)
			return true;
	}

	return false;
}

void Game::DrawInstancedModelBatchPBR(
	const InstancedModelBatch& batch,
	const Camera3D& cam,
	bool transparentPass,
	const std::vector<Matrix>* overrideTransforms
) const
{
	if (!batch.loaded)
		return;

	const std::vector<Matrix>& transformsToDraw =
		overrideTransforms != nullptr
		? *overrideTransforms
		: batch.transforms;

	if (transformsToDraw.empty())
		return;

	if (transparentPass && !InstancedBatchHasTransparentMeshes(batch))
		return;

	PrepareInstancedPBRShader(cam);

	const Model& model = batch.model;

	for (int meshIndex = 0; meshIndex < model.meshCount; meshIndex++)
	{
		int materialIndex = 0;

		if (model.meshMaterial != nullptr)
			materialIndex = model.meshMaterial[meshIndex];

		if (materialIndex < 0 || materialIndex >= model.materialCount)
			materialIndex = 0;

		int alphaMode = 0;
		float alphaCutoff = 0.5f;
		float baseAlpha = 1.0f;
		float reflectionStrength = 0.025f;

		if (meshIndex < (int)batch.meshAlphaModes.size())
			alphaMode = batch.meshAlphaModes[meshIndex];

		if (meshIndex < (int)batch.meshAlphaCutoffs.size())
			alphaCutoff = batch.meshAlphaCutoffs[meshIndex];

		if (meshIndex < (int)batch.meshBaseAlphas.size())
			baseAlpha = batch.meshBaseAlphas[meshIndex];

		if (meshIndex < (int)batch.meshReflectionStrengths.size())
			reflectionStrength = batch.meshReflectionStrengths[meshIndex];

		bool materialIsTransparent = alphaMode == 2;

		// Opaque pass draws opaque + masked.
		if (!transparentPass && materialIsTransparent)
			continue;

		// Transparent pass draws only blended materials.
		if (transparentPass && !materialIsTransparent)
			continue;

		const Material& mat = model.materials[materialIndex];
		bool cheapOpaqueMode = (&batch == &gachaBallInstanceBatch);

		ApplyInstancedPBRMaterialUniforms(
			mat,
			model.meshes[meshIndex],
			alphaMode,
			alphaCutoff,
			baseAlpha,
			reflectionStrength,
			cheapOpaqueMode
		);

		DrawMeshInstanced(
			model.meshes[meshIndex],
			mat,
			transformsToDraw.data(),
			(int)transformsToDraw.size()
		);
	}
}

void Game::DrawInstancedModelBatchPBRVisible(
	const InstancedModelBatch& batch,
	const Camera3D& cam,
	bool transparentPass,
	const std::vector<Matrix>& visibleTransforms
) const
{
	if (!batch.loaded)
		return;

	if (visibleTransforms.empty())
		return;

	if (transparentPass && !InstancedBatchHasTransparentMeshes(batch))
		return;

	PrepareInstancedPBRShader(cam);

	const Model& model = batch.model;

	for (int meshIndex = 0; meshIndex < model.meshCount; meshIndex++)
	{
		int materialIndex = 0;

		if (model.meshMaterial != nullptr)
			materialIndex = model.meshMaterial[meshIndex];

		if (materialIndex < 0 || materialIndex >= model.materialCount)
			materialIndex = 0;

		int alphaMode = 0;
		float alphaCutoff = 0.5f;
		float baseAlpha = 1.0f;
		float reflectionStrength = 0.025f;

		if (meshIndex < (int)batch.meshAlphaModes.size())
			alphaMode = batch.meshAlphaModes[meshIndex];

		if (meshIndex < (int)batch.meshAlphaCutoffs.size())
			alphaCutoff = batch.meshAlphaCutoffs[meshIndex];

		if (meshIndex < (int)batch.meshBaseAlphas.size())
			baseAlpha = batch.meshBaseAlphas[meshIndex];

		if (meshIndex < (int)batch.meshReflectionStrengths.size())
			reflectionStrength = batch.meshReflectionStrengths[meshIndex];

		bool materialIsTransparent = alphaMode == 2;

		if (!transparentPass && materialIsTransparent)
			continue;

		if (transparentPass && !materialIsTransparent)
			continue;

		const Material& mat = model.materials[materialIndex];

		ApplyInstancedPBRMaterialUniforms(
			mat,
			model.meshes[meshIndex],
			alphaMode,
			alphaCutoff,
			baseAlpha,
			reflectionStrength
		);

		DrawMeshInstanced(
			model.meshes[meshIndex],
			mat,
			visibleTransforms.data(),
			(int)visibleTransforms.size()
		);
	}
}
void Game::DrawGachaMachineInstanceTest(const Camera3D& cam) const
{
	const std::vector<Matrix>& visible =
		GetVisibleInstancedTransforms(
			gachaInstanceBatch,
			cam,
			2.5f,
			45.0f
		);

	DrawInstancedModelBatchPBRVisible(gachaInstanceBatch, cam, false, visible);

	if (!InstancedBatchHasTransparentMeshes(gachaInstanceBatch))
		return;

	BeginBlendMode(BLEND_ALPHA);
	rlDisableDepthMask();

	DrawInstancedModelBatchPBRVisible(gachaInstanceBatch, cam, true, visible);

	rlEnableDepthMask();
	EndBlendMode();
}
void Game::DrawGachaBallInstances(const Camera3D& cam) const
{
	return;
	/*
		BuildVisibleGachaBallTransforms(cam);

	if (visibleGachaBallTransformsScratch.empty())
		return;

	DrawInstancedModelBatchPBR(
		gachaBallInstanceBatch,
		cam,
		false,
		&visibleGachaBallTransformsScratch
	);

	if (!InstancedBatchHasTransparentMeshes(gachaBallInstanceBatch))
		return;

	BeginBlendMode(BLEND_ALPHA);
	rlDisableDepthMask();

	DrawInstancedModelBatchPBR(
		gachaBallInstanceBatch,
		cam,
		true,
		&visibleGachaBallTransformsScratch
	);

	rlEnableDepthMask();
	EndBlendMode();
	*/

}

void Game::DrawCustomerAIDebug() const
{
	const float gachaNavPadding = 0.18f;

	// ----------------------------------------------------
	// Instanced gacha AI collision boxes
	// ----------------------------------------------------
	for (const InstancedProp& prop : instancedProps)
	{
		if (!prop.visible) continue;
		if (!prop.hasCollision) continue;
		if (!prop.blocksPlayer) continue;

		if (prop.type != InstancePropType::GachaMachine)
			continue;

		Vector3 scaledSize = {
			fabsf(prop.colliderSize.x * prop.scale.x),
			fabsf(prop.colliderSize.y * prop.scale.y),
			fabsf(prop.colliderSize.z * prop.scale.z)
		};

		Vector3 half = {
			scaledSize.x * 0.5f + gachaNavPadding,
			scaledSize.y * 0.5f,
			scaledSize.z * 0.5f + gachaNavPadding
		};

		Quaternion rot = QuaternionFromEuler(
			prop.rotationDeg.x * DEG2RAD,
			prop.rotationDeg.y * DEG2RAD,
			prop.rotationDeg.z * DEG2RAD
		);

		Vector3 scaledOffset = {
			prop.colliderOffset.x * prop.scale.x,
			prop.colliderOffset.y * prop.scale.y,
			prop.colliderOffset.z * prop.scale.z
		};

		Vector3 rotatedOffset = Vector3RotateByQuaternion(scaledOffset, rot);
		Vector3 center = Vector3Add(prop.position, rotatedOffset);

		BoundingBox box{};
		box.min = {
			center.x - half.x,
			center.y - half.y,
			center.z - half.z
		};

		box.max = {
			center.x + half.x,
			center.y + half.y,
			center.z + half.z
		};

		DrawBoundingBox(box, ORANGE);
	}

	// ----------------------------------------------------
	// Customer collision boxes
	// ----------------------------------------------------
	for (const Customer& customer : customers)
	{
		DrawBoundingBox(
			MakeCustomerCollisionBoxAt(customer.position),
			RED
		);
	}

	// ----------------------------------------------------
	// Player collision box
	// ----------------------------------------------------
	DrawBoundingBox(
		MakePlayerCollisionBoxAt(player.m_pos),
		SKYBLUE
	);
}

void Game::ApplyInstancedPBRMaterialUniforms(
	const Material& mat,
	const Mesh& mesh,
	int alphaMode,
	float alphaCutoff,
	float baseAlpha,
	float reflectionStrengthValue,
	bool cheapOpaqueMode
) const
{
	if (instancedPbrShader.id == 0)
		return;

	bool hasAlbedoMap = mat.maps[MATERIAL_MAP_ALBEDO].texture.id > 1;
	bool hasNormalMap = mat.maps[MATERIAL_MAP_NORMAL].texture.id > 1;
	bool hasMRAMap = mat.maps[MATERIAL_MAP_METALNESS].texture.id > 1;
	bool hasEmissiveMap = mat.maps[MATERIAL_MAP_EMISSION].texture.id > 1;

	bool meshHasTangents = mesh.tangents != nullptr;

	int useTexAlbedo = hasAlbedoMap ? 1 : 0;
	int useTexNormal = (hasNormalMap && meshHasTangents) ? 1 : 0;
	int useTexMRA = hasMRAMap ? 1 : 0;
	int useTexEmissive = hasEmissiveMap ? 1 : 0;

	int useTexMetallic = 0;
	int useTexRoughness = 0;
	int useTexAO = 0;
	int useGltfMetallicRoughness = hasMRAMap ? 1 : 0;



	float metallic = hasMRAMap ? mat.maps[MATERIAL_MAP_METALNESS].value : 0.0f;
	float roughness = hasMRAMap ? mat.maps[MATERIAL_MAP_ROUGHNESS].value : 0.65f;
	float ao = 1.0f;
	float normalStrength = useTexNormal ? 1.0f : 0.0f;
	float emissivePower = hasEmissiveMap ? 1.0f : 0.0f;

	if (alphaMode == 2)
	{
		// Transparent instanced meshes are expensive.
		// Keep the albedo, but avoid normal/MRA/AO/emissive texture work.
		useTexNormal = 0;
		useTexMRA = 0;
		useTexMetallic = 0;
		useTexRoughness = 0;
		useTexAO = 0;
		useTexEmissive = 0;
		useGltfMetallicRoughness = 0;

		reflectionStrengthValue = 0.02f;
	}

	Vector4 albedoColor = ColorNormalize(mat.maps[MATERIAL_MAP_ALBEDO].color);
	albedoColor.w = baseAlpha;

	Vector4 emissiveColor = ColorNormalize(mat.maps[MATERIAL_MAP_EMISSION].color);

	int receiveShadows = alphaMode == 2 ? 0 : 1;


	if (cheapOpaqueMode)
	{
		useTexNormal = 0;
		useTexMRA = 0;
		useTexMetallic = 0;
		useTexRoughness = 0;
		useTexAO = 0;
		useTexEmissive = 0;
		useGltfMetallicRoughness = 0;

		metallic = 0.0f;
		roughness = 0.65f;
		ao = 1.0f;
		normalStrength = 0.0f;
		emissivePower = 0.0f;
		reflectionStrengthValue = 0.0f;

		alphaMode = 0;
		baseAlpha = 1.0f;
		receiveShadows = 0;
	}

	SetShaderValueIfValid(instancedPbrShader, instancedUseTexAlbedoLoc, &useTexAlbedo, SHADER_UNIFORM_INT);
	SetShaderValueIfValid(instancedPbrShader, instancedUseTexNormalLoc, &useTexNormal, SHADER_UNIFORM_INT);
	SetShaderValueIfValid(instancedPbrShader, instancedUseTexMRALoc, &useTexMRA, SHADER_UNIFORM_INT);
	SetShaderValueIfValid(instancedPbrShader, instancedUseTexEmissiveLoc, &useTexEmissive, SHADER_UNIFORM_INT);

	SetShaderValueIfValid(instancedPbrShader, instancedUseTexMetallicLoc, &useTexMetallic, SHADER_UNIFORM_INT);
	SetShaderValueIfValid(instancedPbrShader, instancedUseTexRoughnessLoc, &useTexRoughness, SHADER_UNIFORM_INT);
	SetShaderValueIfValid(instancedPbrShader, instancedUseTexAOLoc, &useTexAO, SHADER_UNIFORM_INT);
	SetShaderValueIfValid(instancedPbrShader, instancedUseGltfMetallicRoughnessLoc, &useGltfMetallicRoughness, SHADER_UNIFORM_INT);

	SetShaderValueIfValid(instancedPbrShader, instancedMetallicValueLoc, &metallic, SHADER_UNIFORM_FLOAT);
	SetShaderValueIfValid(instancedPbrShader, instancedRoughnessValueLoc, &roughness, SHADER_UNIFORM_FLOAT);
	SetShaderValueIfValid(instancedPbrShader, instancedAoValueLoc, &ao, SHADER_UNIFORM_FLOAT);
	SetShaderValueIfValid(instancedPbrShader, instancedNormalValueLoc, &normalStrength, SHADER_UNIFORM_FLOAT);
	SetShaderValueIfValid(instancedPbrShader, instancedEmissivePowerLoc, &emissivePower, SHADER_UNIFORM_FLOAT);
	SetShaderValueIfValid(instancedPbrShader, instancedReflectionStrengthLoc, &reflectionStrengthValue, SHADER_UNIFORM_FLOAT);

	SetShaderValueIfValid(instancedPbrShader, instancedAlbedoColorLoc, &albedoColor, SHADER_UNIFORM_VEC4);
	SetShaderValueIfValid(instancedPbrShader, instancedEmissiveColorLoc, &emissiveColor, SHADER_UNIFORM_VEC4);

	SetShaderValueIfValid(instancedPbrShader, instancedAlphaModeLoc, &alphaMode, SHADER_UNIFORM_INT);
	SetShaderValueIfValid(instancedPbrShader, instancedAlphaCutoffLoc, &alphaCutoff, SHADER_UNIFORM_FLOAT);
	SetShaderValueIfValid(instancedPbrShader, instancedReceiveShadowsLoc, &receiveShadows, SHADER_UNIFORM_INT);


}


void Game::UnloadGachaMachineInstanceTest()
{
	if (gachaInstanceBatch.loaded)
	{
		UnloadModel(gachaInstanceBatch.model);
		gachaInstanceBatch.model = {};
		gachaInstanceBatch.transforms.clear();
		gachaInstanceBatch.loaded = false;
	}

	if (instancingShader.id != 0)
	{
		UnloadShader(instancingShader);
		instancingShader = {};
	}
}
bool Game::LoadGachaBallLocalTransforms(const char* path)
{
	std::ifstream file(path);

	if (!file.is_open())
	{
		TraceLog(LOG_WARNING, "Failed to open gacha ball transform file: %s", path);
		return false;
	}

	gachaBallLocalTransforms.clear();

	std::string name;

	while (file >> name)
	{
		BallLocalTransform tr{};
		tr.name = name;

		file >> tr.position.x >> tr.position.y >> tr.position.z;
		file >> tr.rotation.w >> tr.rotation.x >> tr.rotation.y >> tr.rotation.z;
		file >> tr.scale.x >> tr.scale.y >> tr.scale.z;

		tr.rotation = QuaternionNormalize(tr.rotation);
		tr.localMatrix = MakeMatrixFromTRS(tr.position, tr.rotation, tr.scale);

		gachaBallLocalTransforms.push_back(tr);
	}

	TraceLog(
		LOG_INFO,
		"Loaded %i gacha ball local transforms.",
		(int)gachaBallLocalTransforms.size()
	);

	return !gachaBallLocalTransforms.empty();
}
void Game::LoadGachaBallInstanceTest()
{
	if (instancedPbrShader.id == 0)
	{
		TraceLog(LOG_WARNING, "instancedPbrShader is not loaded before LoadGachaBallInstanceTest().");
		return;
	}

	gachaBallInstanceBatch.model = LoadModel("Models/GachaBalls2.glb");
	gachaBallInstanceBatch.loaded = gachaBallInstanceBatch.model.meshCount > 0;

	if (!gachaBallInstanceBatch.loaded)
	{
		TraceLog(LOG_WARNING, "Failed to load Models/GachaBalls.glb");
		return;
	}

	for (int i = 0; i < gachaBallInstanceBatch.model.materialCount; i++)
	{
		gachaBallInstanceBatch.model.materials[i].shader = instancedPbrShader;
	}

	BuildInstancedBatchAlphaSettings(gachaBallInstanceBatch);

	// Force gacha balls to render as fully opaque.
	gachaBallInstanceBatch.transparentMaterialIndices.clear();

	for (int& mode : gachaBallInstanceBatch.meshAlphaModes)
	{
		mode = 0; // opaque
	}

	for (float& cutoff : gachaBallInstanceBatch.meshAlphaCutoffs)
	{
		cutoff = 0.5f;
	}

	for (float& alpha : gachaBallInstanceBatch.meshBaseAlphas)
	{
		alpha = 1.0f;
	}

	for (float& reflection : gachaBallInstanceBatch.meshReflectionStrengths)
	{
		reflection = 0.025f;
	}

	// Also force material color alpha to opaque.
	for (int i = 0; i < gachaBallInstanceBatch.model.materialCount; i++)
	{
		Material& mat = gachaBallInstanceBatch.model.materials[i];

		mat.maps[MATERIAL_MAP_ALBEDO].color.a = 255;
		mat.maps[MATERIAL_MAP_METALNESS].color.a = 255;
		mat.maps[MATERIAL_MAP_ROUGHNESS].color.a = 255;
		mat.maps[MATERIAL_MAP_OCCLUSION].color.a = 255;
		mat.maps[MATERIAL_MAP_EMISSION].color.a = 255;

		mat.shader = instancedPbrShader;
	}

	LoadGachaBallLocalTransforms("Data/gacha_ball_local_transforms.txt");

	BuildGachaBallWorldTransforms();

	TraceLog(
		LOG_INFO,
		"Gacha ball instancing loaded: meshes=%i materials=%i localBalls=%i worldInstances=%i",
		gachaBallInstanceBatch.model.meshCount,
		gachaBallInstanceBatch.model.materialCount,
		(int)gachaBallLocalTransforms.size(),
		(int)gachaBallInstanceBatch.transforms.size()
	);
}
void Game::BuildGachaBallWorldTransforms()
{
	gachaBallInstanceBatch.transforms.clear();

	if (!gachaInstanceBatch.transforms.empty())
	{
		for (const Matrix& machineWorld : gachaInstanceBatch.transforms)
		{
			for (const BallLocalTransform& ball : gachaBallLocalTransforms)
			{
				Matrix ballWorld = MatrixMultiply(ball.localMatrix, machineWorld);
				gachaBallInstanceBatch.transforms.push_back(ballWorld);
			}
		}

		return;
	}

	Matrix machineWorld = MakeInstanceTRS(
		{ 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f },
		{ 1.0f, 1.0f, 1.0f }
	);

	for (const BallLocalTransform& ball : gachaBallLocalTransforms)
	{
		Matrix ballWorld = MatrixMultiply(ball.localMatrix, machineWorld);
		gachaBallInstanceBatch.transforms.push_back(ballWorld);
	}
}
void Game::RefreshScenePropRenderBounds(SceneProp& prop)
{
	prop.renderBoundsReady = false;
	prop.localRenderRadius = 1.0f;
	prop.localRenderCenter = { 0.0f, 0.0f, 0.0f };
	prop.localRenderBounds = {};

	if (prop.model == nullptr)
		return;

	BoundingBox bounds = GetModelBoundingBox(*prop.model);

	Vector3 size = {
		bounds.max.x - bounds.min.x,
		bounds.max.y - bounds.min.y,
		bounds.max.z - bounds.min.z
	};

	Vector3 center = {
		(bounds.min.x + bounds.max.x) * 0.5f,
		(bounds.min.y + bounds.max.y) * 0.5f,
		(bounds.min.z + bounds.max.z) * 0.5f
	};

	if (!IsFiniteVector3(size) || !IsFiniteVector3(center))
		return;

	prop.localRenderBounds = bounds;
	prop.localRenderCenter = center;
	prop.localRenderRadius = GetPropApproxRadius(size);

	prop.localRenderRadius = fmaxf(prop.localRenderRadius, 0.35f);
	prop.localRenderRadius += 0.25f;

	prop.renderBoundsReady = true;
}

void Game::RefreshAllScenePropRenderBounds()
{
	int count = (int)sceneProps.size();

	for (int i = 0; i < count; i++)
	{
		if (i % 25 == 0)
		{
			float t = count > 0 ? (float)i / (float)count : 1.0f;

			LoadingPulse(
				TextFormat("Refreshing scene prop bounds... %i/%i", i, count),
				Lerp(0.74f, 0.76f, t),
				false
			);
		}

		RefreshScenePropRenderBounds(sceneProps[i]);
	}

	LoadingPulse("Scene prop bounds refreshed.", 0.76f, true);
}

void Game::UnloadGachaBallInstances()
{
	if (gachaBallInstanceBatch.loaded)
	{
		UnloadModel(gachaBallInstanceBatch.model);
		gachaBallInstanceBatch.model = {};
		gachaBallInstanceBatch.transforms.clear();
		gachaBallInstanceBatch.loaded = false;
	}

	gachaBallLocalTransforms.clear();

	if (gachaBallInstancingShader.id != 0)
	{
		UnloadShader(gachaBallInstancingShader);
		gachaBallInstancingShader = {};
	}
}
void Game::LoadBasketInstanceTest()
{
	if (instancedPbrShader.id == 0)
	{
		TraceLog(LOG_WARNING, "instancedPbrShader is not loaded before LoadBasketInstanceTest().");
		return;
	}

	basketInstanceBatch.model = LoadModel("Models/Basket.glb");
	basketInstanceBatch.loaded = basketInstanceBatch.model.meshCount > 0;

	if (!basketInstanceBatch.loaded)
	{
		TraceLog(LOG_WARNING, "Failed to load Models/Basket.glb");
		return;
	}

	for (int i = 0; i < basketInstanceBatch.model.materialCount; i++)
	{
		basketInstanceBatch.model.materials[i].shader = instancedPbrShader;
	}

	BuildInstancedBatchAlphaSettings(basketInstanceBatch);
	ComputeBasketInstanceAutoCollider();

	TraceLog(
		LOG_INFO,
		"Basket instance model loaded: meshes=%i materials=%i",
		basketInstanceBatch.model.meshCount,
		basketInstanceBatch.model.materialCount
	);
}
void Game::ComputeBasketInstanceAutoCollider()
{
	if (!basketInstanceBatch.loaded)
		return;

	GetModelLocalBoundsCollider(
		&basketInstanceBatch.model,
		basketInstanceColliderSize,
		basketInstanceColliderOffset
	);

	basketInstanceColliderReady = true;

	TraceLog(
		LOG_INFO,
		"Basket collider: size=(%.3f %.3f %.3f), offset=(%.3f %.3f %.3f)",
		basketInstanceColliderSize.x,
		basketInstanceColliderSize.y,
		basketInstanceColliderSize.z,
		basketInstanceColliderOffset.x,
		basketInstanceColliderOffset.y,
		basketInstanceColliderOffset.z
	);
}
void Game::DrawBasketInstances(const Camera3D& cam) const
{
	BuildVisibleBasketInstanceTransforms(cam);

	if (visibleBasketTransformsScratch.empty())
		return;

	DrawInstancedModelBatchPBR(
		basketInstanceBatch,
		cam,
		false,
		&visibleBasketTransformsScratch
	);

	if (!InstancedBatchHasTransparentMeshes(basketInstanceBatch))
		return;

	BeginBlendMode(BLEND_ALPHA);
	rlDisableDepthMask();

	DrawInstancedModelBatchPBR(
		basketInstanceBatch,
		cam,
		true,
		&visibleBasketTransformsScratch
	);

	rlEnableDepthMask();
	EndBlendMode();
}
void Game::DrawBasketCartridgeInstances(const Camera3D& cam) const
{
	BuildVisibleBasketInstanceTransforms(cam);

	if (visibleBasketCartridgeTransformsScratch.empty())
		return;

	DrawInstancedModelBatchPBR(
		basketCartridgeInstanceBatch,
		cam,
		false,
		&visibleBasketCartridgeTransformsScratch
	);

	if (!InstancedBatchHasTransparentMeshes(basketCartridgeInstanceBatch))
		return;

	BeginBlendMode(BLEND_ALPHA);
	rlDisableDepthMask();

	DrawInstancedModelBatchPBR(
		basketCartridgeInstanceBatch,
		cam,
		true,
		&visibleBasketCartridgeTransformsScratch
	);

	rlEnableDepthMask();
	EndBlendMode();
}
void Game::UnloadBasketInstances()
{
	if (basketInstanceBatch.loaded)
	{
		UnloadModel(basketInstanceBatch.model);
		basketInstanceBatch.model = {};
		basketInstanceBatch.transforms.clear();
		basketInstanceBatch.loaded = false;
	}

	if (basketCartridgeInstanceBatch.loaded)
	{
		UnloadModel(basketCartridgeInstanceBatch.model);
		basketCartridgeInstanceBatch.model = {};
		basketCartridgeInstanceBatch.transforms.clear();
		basketCartridgeInstanceBatch.loaded = false;
	}

	basketCartridgeLocalTransforms.clear();
}

bool Game::LoadBasketCartridgeLocalTransforms(const char* path)
{
	std::ifstream file(path);

	if (!file.is_open())
	{
		TraceLog(LOG_WARNING, "Failed to open basket cartridge transform file: %s", path);
		return false;
	}

	basketCartridgeLocalTransforms.clear();

	std::string name;

	while (file >> name)
	{
		BallLocalTransform tr{};
		tr.name = name;

		file >> tr.position.x >> tr.position.y >> tr.position.z;
		file >> tr.rotation.w >> tr.rotation.x >> tr.rotation.y >> tr.rotation.z;
		file >> tr.scale.x >> tr.scale.y >> tr.scale.z;

		tr.rotation = QuaternionNormalize(tr.rotation);
		tr.localMatrix = MakeMatrixFromTRS(tr.position, tr.rotation, tr.scale);

		basketCartridgeLocalTransforms.push_back(tr);
	}

	TraceLog(
		LOG_INFO,
		"Loaded %i basket cartridge local transforms.",
		(int)basketCartridgeLocalTransforms.size()
	);

	return !basketCartridgeLocalTransforms.empty();
}

void Game::LoadBasketCartridgeInstanceTest()
{
	if (instancedPbrShader.id == 0)
	{
		TraceLog(LOG_WARNING, "instancedPbrShader is not loaded before LoadBasketCartridgeInstanceTest().");
		return;
	}

	basketCartridgeInstanceBatch.model = LoadModel("Models/Game.glb");
	basketCartridgeInstanceBatch.loaded =
		basketCartridgeInstanceBatch.model.meshCount > 0;

	if (!basketCartridgeInstanceBatch.loaded)
	{
		TraceLog(LOG_WARNING, "Failed to load Models/Game.glb");
		return;
	}

	for (int i = 0; i < basketCartridgeInstanceBatch.model.materialCount; i++)
	{
		basketCartridgeInstanceBatch.model.materials[i].shader = instancedPbrShader;
	}

	BuildInstancedBatchAlphaSettings(basketCartridgeInstanceBatch);

	LoadBasketCartridgeLocalTransforms("Data/basket_box_local_transforms.txt");

	BuildBasketCartridgeWorldTransforms();

	TraceLog(
		LOG_INFO,
		"Basket cartridge model loaded: meshes=%i materials=%i localCartridges=%i worldInstances=%i",
		basketCartridgeInstanceBatch.model.meshCount,
		basketCartridgeInstanceBatch.model.materialCount,
		(int)basketCartridgeLocalTransforms.size(),
		(int)basketCartridgeInstanceBatch.transforms.size()
	);
}

void Game::BuildVisibleBasketInstanceTransforms(const Camera3D& cam) const
{
	visibleBasketTransformsScratch.clear();
	visibleBasketCartridgeTransformsScratch.clear();

	if (basketInstanceBatch.transforms.empty())
		return;

	const float basketCullRadius = 1.4f;
	const float maxBasketDrawDistance = 35.0f;

	for (const Matrix& basketWorld : basketInstanceBatch.transforms)
	{
		Vector3 basketCenter = GetMatrixTranslationSafe(basketWorld);

		if (!IsSphereInCameraView(
			cam,
			basketCenter,
			basketCullRadius,
			maxBasketDrawDistance
		))
		{
			continue;
		}

		visibleBasketTransformsScratch.push_back(basketWorld);

		for (const BallLocalTransform& cartridge : basketCartridgeLocalTransforms)
		{
			Matrix cartridgeWorld = MatrixMultiply(
				cartridge.localMatrix,
				basketWorld
			);

			visibleBasketCartridgeTransformsScratch.push_back(cartridgeWorld);
		}
	}
}

void Game::BuildBasketCartridgeWorldTransforms()
{
	basketCartridgeInstanceBatch.transforms.clear();

	for (const InstancedProp& prop : instancedProps)
	{
		if (!prop.visible)
			continue;

		if (prop.type != InstancePropType::Basket)
			continue;

		Matrix basketWorld = MakeInstanceTRS(
			prop.position,
			prop.rotationDeg,
			prop.scale
		);

		for (const BallLocalTransform& cartridge : basketCartridgeLocalTransforms)
		{
			Matrix cartridgeWorld = MatrixMultiply(
				cartridge.localMatrix,
				basketWorld
			);

			basketCartridgeInstanceBatch.transforms.push_back(cartridgeWorld);
		}
	}
}

void Game::StartCustomerDialogue(int customerIndex)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	Customer& customer = customers[customerIndex];

	if (dialogueScripts.find(customer.dialogueScriptId) == dialogueScripts.end())
		return;

	//StopPlayerMovementForDialogue();

	customer.hasMoveTarget = false;
	customer.pathWaypoints.clear();
	customer.pathWaypointCursor = 0;
	customer.movementPauseTimer = 0.0f;

	SetCustomerYawTowards(customerIndex, player.m_pos);

	StartCustomerDialogueFocus(customerIndex);

	//customerDialogueCameraLock = true;
	//customerDialogueLookAtIndex = customerIndex;

	dialogueActive = true;
	dialogueCustomerIndex = customerIndex;
	activeDialogueCustomerIndex = customerIndex;
	activeDialogueScriptId = customer.dialogueScriptId;

	currentDialogueNode = 0;
	selectedDialogueChoice = 0;

	SetDialogueNode(currentDialogueNode);
}

static float Smooth01Local(float t)
{
	t = Clamp(t, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}
void Game::StartOpeningCinematicOnce()
{
	if (openingCinematicPlayedThisGame)
		return;

	openingCinematicPlayedThisGame = true;

	StartOpeningCinematic();
}
void Game::StartOpeningCinematic()
{
	openingCinematicActive = true;
	openingCinematicTimer = 0.0f;

	Vector3 eye{
		player.m_pos.x,
		player.m_pos.y + (BOTTOM_HEIGHT + headLerp),
		player.m_pos.z
	};

	Vector3 forward = NormalizeSafe(GetCameraForward());
	forward.y = 0.0f;

	if (Vector3Length(forward) <= 0.0001f)
	{
		forward = { 0.0f, 0.0f, -1.0f };
	}
	else
	{
		forward = Vector3Normalize(forward);
	}

	openingLookUpTarget = Vector3Add(
		Vector3Add(eye, Vector3Scale(forward, 5.0f)),
		Vector3{ 0.0f, 2.6f, 0.0f }
	);

	openingLookDownTarget = Vector3Add(
		Vector3Add(eye, Vector3Scale(forward, 4.0f)),
		Vector3{ 0.0f, -0.45f, 0.0f }
	);

	StopPlayerMovementForDialogue();

	StartSelfDialogueLines(
		std::vector<std::string>{
		"I have to keep this store running.",
			"Every customer matters. Every sale matters."
	},
		"__opening_self"
	);
}
void Game::UpdateOpeningCinematic(float dt)
{
	if (!openingCinematicActive)
		return;

	StopPlayerMovementForDialogue();

	camera.position = {
		player.m_pos.x,
		player.m_pos.y + (BOTTOM_HEIGHT + headLerp),
		player.m_pos.z
	};

	openingCinematicTimer += dt;

	float t = openingCinematicTimer / openingCinematicDuration;
	t = Clamp(t, 0.0f, 1.0f);

	Vector3 target{};

	if (t < 0.30f)
	{
		target = openingLookUpTarget;
	}
	else
	{
		float downT = Smooth01Local((t - 0.30f) / 0.70f);

		target = Vector3Lerp(
			openingLookUpTarget,
			openingLookDownTarget,
			downT
		);
	}

	ApplyCameraLookAt(target, dt, 2.4f);

	if (t >= 1.0f)
	{
		openingCinematicActive = false;
		SyncPlayerLookFromCameraTarget();
	}
}

bool Game::IsCinematicCameraActive() const
{
	return openingCinematicActive || cinematicCamera.active;
}

void Game::StartCinematicLookAt(
	Vector3 target,
	float duration,
	bool lockPlayerMovement,
	bool syncPlayerLookAtEnd
)
{
	cinematicCamera.active = true;
	cinematicCamera.lockPlayerMovement = lockPlayerMovement;
	cinematicCamera.syncPlayerLookAtEnd = syncPlayerLookAtEnd;

	cinematicCamera.timer = 0.0f;
	cinematicCamera.duration = fmaxf(duration, 0.05f);

	cinematicCamera.startTarget = camera.target;
	cinematicCamera.endTarget = target;

	if (lockPlayerMovement)
	{
		StopPlayerMovementForDialogue();
	}
}

void Game::UpdateCinematicCamera(float dt)
{
	if (!cinematicCamera.active)
		return;

	if (cinematicCamera.lockPlayerMovement)
	{
		StopPlayerMovementForDialogue();
	}

	camera.position = {
		player.m_pos.x,
		player.m_pos.y + (BOTTOM_HEIGHT + headLerp),
		player.m_pos.z
	};

	cinematicCamera.timer += dt;

	float t = cinematicCamera.timer / cinematicCamera.duration;
	t = Clamp(t, 0.0f, 1.0f);

	float smoothT = Smooth01Local(t);

	camera.target = Vector3Lerp(
		cinematicCamera.startTarget,
		cinematicCamera.endTarget,
		smoothT
	);

	if (t >= 1.0f)
	{
		cinematicCamera.active = false;

		if (cinematicCamera.syncPlayerLookAtEnd)
		{
			SyncPlayerLookFromCameraTarget();
		}
	}
}

float Game::AngleDeltaRad(float a, float b) const
{
	float d = a - b;

	while (d > PI) d -= PI * 2.0f;
	while (d < -PI) d += PI * 2.0f;

	return d;
}
Vector2 Game::GetLookAnglesToWorldPoint(Vector3 target) const
{
	Vector3 eye{
		player.m_pos.x,
		player.m_pos.y + (BOTTOM_HEIGHT + headLerp),
		player.m_pos.z
	};

	Vector3 dir = Vector3Subtract(target, eye);

	if (Vector3Length(dir) <= 0.0001f)
	{
		return { player.yaw, player.pitch };
	}

	dir = Vector3Normalize(dir);

	Vector3 flat{
		dir.x,
		0.0f,
		dir.z
	};

	float yaw = player.yaw;

	if (Vector3Length(flat) > 0.0001f)
	{
		flat = Vector3Normalize(flat);

		// Matches your updateCameraFPS() convention:
		// targetOffset is {0, 0, -1}.
		yaw = atan2f(
			-flat.x,
			-flat.z
		);
	}

	float pitch = -asinf(
		Clamp(dir.y, -0.98f, 0.98f)
	);

	return { yaw, pitch };
}

void Game::UpdateCustomerDialogueFocus(float dt)
{
	if (!customerDialogueFocusActive)
		return;

	if (!dialogueActive)
	{
		EndCustomerDialogueFocus();
		return;
	}

	if (customerDialogueFocusIndex < 0 ||
		customerDialogueFocusIndex >= (int)customers.size())
	{
		EndCustomerDialogueFocus();
		return;
	}

	Customer& customer = customers[customerDialogueFocusIndex];

	// Stop movement/pathing only.
	// Do NOT force idle here. Dialogue animation cues handle animation.
	customer.hasMoveTarget = false;
	customer.pathWaypoints.clear();
	customer.pathWaypointCursor = 0;
	customer.movementPauseTimer = 0.0f;

	SetCustomerYawTowards(customerDialogueFocusIndex, player.m_pos);

	Vector3 lookTarget = customer.position;
	lookTarget.y += 1.35f;

	Vector2 desiredCenter = GetLookAnglesToWorldPoint(lookTarget);

	float centerT = 1.0f - expf(-4.0f * dt);

	customerDialogueCenterYaw += AngleDeltaRad(
		desiredCenter.x,
		customerDialogueCenterYaw
	) * centerT;

	customerDialogueCenterPitch = Lerp(
		customerDialogueCenterPitch,
		desiredCenter.y,
		centerT
	);

	if (customerDialogueLookInputThisFrame)
	{
		customerDialogueIdleTimer = 0.0f;
	}
	else
	{
		customerDialogueIdleTimer += dt;
	}

	const float yawLimit = customerDialogueYawLimitDeg * DEG2RAD;
	const float pitchLimit = customerDialoguePitchLimitDeg * DEG2RAD;

	float yawOffset = AngleDeltaRad(
		player.yaw,
		customerDialogueCenterYaw
	);

	float pitchOffset = player.pitch - customerDialogueCenterPitch;

	yawOffset = Clamp(yawOffset, -yawLimit, yawLimit);
	pitchOffset = Clamp(pitchOffset, -pitchLimit, pitchLimit);

	player.yaw = customerDialogueCenterYaw + yawOffset;
	player.pitch = customerDialogueCenterPitch + pitchOffset;

	if (customerDialogueIdleTimer >= customerDialogueReturnDelay)
	{
		float returnT = 1.0f - expf(-customerDialogueReturnSpeed * dt);

		player.yaw += AngleDeltaRad(
			customerDialogueCenterYaw,
			player.yaw
		) * returnT;

		player.pitch = Lerp(
			player.pitch,
			customerDialogueCenterPitch,
			returnT
		);
	}

	customerDialogueLookInputThisFrame = false;

	lean = { 0.0f, 0.0f };
	walkLerp = 0.0f;
}

void Game::StartCustomerDialogueFocus(int customerIndex)
{
	if (customerIndex < 0 || customerIndex >= (int)customers.size())
		return;

	customerDialogueFocusActive = true;
	customerDialogueFocusIndex = customerIndex;
	customerDialogueIdleTimer = customerDialogueReturnDelay;

	Vector3 lookTarget = customers[customerIndex].position;
	lookTarget.y += 1.35f;

	Vector2 center = GetLookAnglesToWorldPoint(lookTarget);

	customerDialogueCenterYaw = center.x;
	customerDialogueCenterPitch = center.y;
}

void Game::UpdateCustomerDialogueLookInputOnly()
{
	customerDialogueLookInputThisFrame = false;

	float oldYaw = player.yaw;
	float oldPitch = player.pitch;

	// This keeps the normal mouse sensitivity / look behavior.
	// Then we cancel movement immediately after.
	player.input();

	customerDialogueLookInputThisFrame =
		fabsf(AngleDeltaRad(player.yaw, oldYaw)) > 0.00001f ||
		fabsf(player.pitch - oldPitch) > 0.00001f;

	// Cancel walking/jumping movement, but keep the yaw/pitch
	// that player.input() just updated.
	StopPlayerMovementForDialogue();
}

void Game::EndCustomerDialogueFocus()
{
	customerDialogueFocusActive = false;
	customerDialogueFocusIndex = -1;
	customerDialogueIdleTimer = 0.0f;
}

void Game::StartSelfInspectDialogueForHeldItem()
{
	if (!hasHeldBody)
		return;

	if (heldScenePropIndex < 0 ||
		heldScenePropIndex >= (int)sceneProps.size())
	{
		return;
	}

	const SceneProp& prop = sceneProps[heldScenePropIndex];

	// Prefer Inspect Dialogue Tag.
	// Fallback to Item Tag, because it is easier to set from the item inspector.
	std::string tag = prop.inspectDialogueTag;

	if (tag.empty())
	{
		tag = prop.itemTag;
	}

	std::string cleanTag = SanitizeVoiceToken(tag);

	std::string sfxOnly = GetInspectSfxForTag(cleanTag);

	if (!sfxOnly.empty())
	{
		if (ShouldInspectSfxLoopForTag(cleanTag))
		{
			PlayLoopingSfx(sfxOnly, true);
		}
		else
		{
			PlaySfx(sfxOnly, true);
		}

		return;
	}

	std::vector<std::string> lines = prop.inspectDialogueLines;

	if (lines.empty())
	{
		lines.push_back("Hmm. I should take a closer look at this.");
	}

	StartSelfDialogueLines(lines, "__self_inspect");
}
bool Game::IsStoreOpenControlProp(const SceneProp& prop) const
{
	return prop.itemTag == "store_open";
}

bool Game::IsStoreCloseControlProp(const SceneProp& prop) const
{
	return prop.itemTag == "store_close";
}



bool Game::IsStoreControlProp(const SceneProp& prop) const
{
	return IsStoreOpenControlProp(prop) || IsStoreCloseControlProp(prop);
}

void Game::MarkStoreControlScenePropCacheDirty()
{
	storeControlScenePropCacheDirty = true;
}

void Game::RebuildStoreControlScenePropCache() const
{
	storeControlScenePropIndices.clear();
	storeControlScenePropIndices.reserve(sceneProps.size());

	for (int i = 0; i < (int)sceneProps.size(); i++)
	{
		const SceneProp& prop = sceneProps[i];

		if (!prop.visible)
			continue;

		if (IsStoreControlProp(prop))
		{
			storeControlScenePropIndices.push_back(i);
		}
	}

	storeControlScenePropCacheSceneCount = (int)sceneProps.size();
	storeControlScenePropCacheDirty = false;

	TraceLog(
		LOG_INFO,
		"Store control cache rebuilt: %i / %i scene props",
		(int)storeControlScenePropIndices.size(),
		(int)sceneProps.size()
	);
}

int Game::FindLookedAtStoreControlProp() const
{
	if (editMode)
		return -1;

	if (cursorUnlocked)
		return -1;

	if (inspectMode)
		return -1;

	if (hasHeldBody)
		return -1;

	if (storeControlScenePropCacheDirty ||
		storeControlScenePropCacheSceneCount != (int)sceneProps.size())
	{
		RebuildStoreControlScenePropCache();
	}

	Vector3 rayOrigin = camera.position;
	Vector3 rayDir = NormalizeSafe(GetCameraForward());

	int bestIndex = -1;
	float bestDistance = 99999.0f;

	const float storeControlInteractDistance = 3.0f;

	for (int propIndex : storeControlScenePropIndices)
	{
		if (propIndex < 0 || propIndex >= (int)sceneProps.size())
			continue;

		const SceneProp& prop = sceneProps[propIndex];

		if (!prop.visible)
			continue;

		Vector3 localSize = prop.colliderSize;
		Vector3 localOffset = prop.colliderOffset;

		if ((localSize.x <= 0.0f ||
			localSize.y <= 0.0f ||
			localSize.z <= 0.0f) &&
			prop.model != nullptr)
		{
			GetModelLocalBoundsCollider(
				prop.model,
				localSize,
				localOffset
			);
		}

		Vector3 size = {
			fabsf(localSize.x * prop.scale.x) + 0.25f,
			fabsf(localSize.y * prop.scale.y) + 0.25f,
			fabsf(localSize.z * prop.scale.z) + 0.25f
		};

		Quaternion rot = QuaternionFromEuler(
			prop.rotationDeg.x * DEG2RAD,
			prop.rotationDeg.y * DEG2RAD,
			prop.rotationDeg.z * DEG2RAD
		);

		Vector3 scaledOffset = {
			localOffset.x * prop.scale.x,
			localOffset.y * prop.scale.y,
			localOffset.z * prop.scale.z
		};

		Vector3 rotatedOffset = Vector3RotateByQuaternion(
			scaledOffset,
			rot
		);

		Vector3 center = Vector3Add(
			prop.position,
			rotatedOffset
		);

		float hitDistance = 0.0f;

		if (RaycastAgainstOBB(
			rayOrigin,
			rayDir,
			center,
			rot,
			size,
			storeControlInteractDistance,
			hitDistance
		))
		{
			if (hitDistance < bestDistance)
			{
				bestDistance = hitDistance;
				bestIndex = propIndex;
			}
		}
	}

	return bestIndex;
}

void Game::OpenStore()
{
	if (storeDayState == StoreDayState::Open)
		return;

	storeDayState = StoreDayState::Open;

	finalScoreVisible = false;
	finalDayResult = {};
	closeShopReason.clear();

	StoreDayConfig cfg = GetCurrentStoreDayConfig();

	customersToProcessBeforeClose = cfg.customerTarget;

	customersSpawnedToday = 0;
	customersCompletedToday = 0;

	buyerSalesToday = 0;
	sellerBuySpendToday = 0;
	sellerPurchasesToday = 0;

	sellerTradeItemsUsedToday.clear();

	storeBudgetAtOpen = storeBudgetYen;

	customerSpawningEnabled = true;
	customerSpawnTimer = 0.0f;

	// Do not spawn instantly. Day 1 should feel slower.
	nextCustomerSpawnDelay = GetCurrentDaySpawnDelay();

	PlaySfx("store_open");

	TraceLog(LOG_INFO, "Store opened.");
}

void Game::BeginStoreClosing(const std::string& reason)
{
	if (storeDayState == StoreDayState::Closing ||
		storeDayState == StoreDayState::Results)
	{
		return;
	}

	storeDayState = StoreDayState::Closing;
	closeShopReason = reason;

	customerSpawningEnabled = false;

	PlaySfx("store_close");

	TraceLog(LOG_INFO, "Store closing: %s", reason.c_str());
}

void Game::AdvanceAfterDayResult()
{
	if (!finalScoreVisible)
		return;

	finalScoreVisible = false;

	if (finalDayResult.passed)
	{
		if (currentStoreDay < maxStoreDays)
		{
			StartDay(currentStoreDay + 1);
			return;
		}

		// Finished Day 5.
		ShowCampaignCompleteScreen(false);
		return;
	}

	// Failed quota: retry same day, keep current budget so mistakes matter.
	StartDay(currentStoreDay);
}

void Game::UpdateStoreDayState(float dt)
{
	if (storeDayState == StoreDayState::Open &&
		customersSpawnedToday >= customersToProcessBeforeClose)
	{
		BeginStoreClosing("Daily customer limit reached.");
	}

	if (storeDayState == StoreDayState::Closing)
	{
		if (CountActiveCustomers() <= 0 && !dialogueActive)
		{
			storeDayState = StoreDayState::Results;
			customerSpawningEnabled = false;

			CaptureFinalDayResult();

			finalScoreVisible = true;
			dayResultTimer = 0.0f;

			PlaySfx("day_result");
		}
	}

	if (finalScoreVisible)
	{
		dayResultTimer += dt;

		if (dayResultTimer >= dayResultAutoAdvanceDelay ||
			IsKeyPressed(KEY_ENTER))
		{
			AdvanceAfterDayResult();
		}
	}
}
const std::vector<Matrix>& Game::GetVisibleInstancedTransforms(
	const InstancedModelBatch& batch,
	const Camera3D& cam,
	float approximateRadius,
	float maxDistance
) const
{
	visibleInstancedTransformsScratch.clear();

	for (const Matrix& transform : batch.transforms)
	{
		Vector3 center = GetMatrixTranslationSafe(transform);

		if (!IsSphereInCameraView(
			cam,
			center,
			approximateRadius,
			maxDistance
		))
		{
			continue;
		}

		visibleInstancedTransformsScratch.push_back(transform);
	}

	return visibleInstancedTransformsScratch;
}
BoundingBox Game::GetCinematicTriggerBounds(
	const CinematicTriggerZone& trigger
) const
{
	Vector3 half{
		trigger.size.x * 0.5f,
		trigger.size.y * 0.5f,
		trigger.size.z * 0.5f
	};

	return {
		{
			trigger.position.x - half.x,
			trigger.position.y - half.y,
			trigger.position.z - half.z
		},
		{
			trigger.position.x + half.x,
			trigger.position.y + half.y,
			trigger.position.z + half.z
		}
	};
}

void Game::ResetCinematicTriggerRuntimeState()
{
	for (CinematicTriggerZone& trigger : cinematicTriggers)
	{
		trigger.triggered = false;
		trigger.wasInside = false;
	}
}

bool Game::IsLookingAtTriggerBox(
	const CinematicTriggerZone& trigger,
	float maxDistance
) const
{
	if (!trigger.enabled)
		return false;

	BoundingBox triggerBox = GetCinematicTriggerBounds(trigger);

	Ray ray{};
	ray.position = camera.position;
	ray.direction = NormalizeSafe(GetCameraForward());

	float hitDistance = 0.0f;

	return RayIntersectsBoundingBoxWithinDistance(
		ray,
		triggerBox,
		maxDistance,
		hitDistance
	);
}

void Game::UpdateLookSoundTriggers(float dt)
{
	std::string wantedSfxId = "";
	float wantedVolume = 0.0f;
	float wantedFadeIn = 0.35f;
	float wantedFadeOut = activeLookSoundFadeOutSeconds;

	bool allowLookSound =
		!editMode &&
		!cursorUnlocked &&
		!inspectMode;

	if (allowLookSound)
	{
		for (const CinematicTriggerZone& trigger : cinematicTriggers)
		{
			if (!trigger.enabled)
				continue;

			if (!trigger.enableLookSound)
				continue;

			if (trigger.lookSoundSfxId.empty())
				continue;

			if (sfx.find(trigger.lookSoundSfxId) == sfx.end())
				continue;

			if (!IsLookingAtTriggerBox(trigger, trigger.lookSoundDistance))
				continue;

			wantedSfxId = trigger.lookSoundSfxId;
			wantedVolume = Clamp(trigger.lookSoundVolume, 0.0f, 1.0f);
			wantedFadeIn = fmaxf(trigger.lookSoundFadeInSeconds, 0.01f);
			wantedFadeOut = fmaxf(trigger.lookSoundFadeOutSeconds, 0.01f);

			break;
		}
	}

	// If we are looking at a valid trigger.
	if (!wantedSfxId.empty())
	{
		// New sound or first sound.
		if (activeLookSoundTriggerSfxId != wantedSfxId)
		{
			// Stop previous look sound immediately when switching targets.
			// For one cat/object this will rarely happen.
			StopActiveLookSoundImmediate();

			activeLookSoundTriggerSfxId = wantedSfxId;
			activeLookSoundVolume = 0.0f;
		}

		activeLookSoundTargetVolume = wantedVolume;
		activeLookSoundFadeInSeconds = wantedFadeIn;
		activeLookSoundFadeOutSeconds = wantedFadeOut;

		auto it = sfx.find(activeLookSoundTriggerSfxId);

		if (it == sfx.end())
		{
			activeLookSoundTriggerSfxId.clear();
			activeLookSoundVolume = 0.0f;
			return;
		}

		Sound& sound = it->second;

		if (!IsSoundPlaying(sound))
		{
			SetSoundVolume(sound, 0.0f);
			PlaySound(sound);
		}

		float fadeSpeed = activeLookSoundTargetVolume / activeLookSoundFadeInSeconds;

		activeLookSoundVolume = MoveTowardsFloatLocal(
			activeLookSoundVolume,
			activeLookSoundTargetVolume,
			fadeSpeed * dt
		);

		SetSoundVolume(sound, activeLookSoundVolume);
		return;
	}

	// No valid look target: fade out current sound.
	if (activeLookSoundTriggerSfxId.empty())
		return;

	auto it = sfx.find(activeLookSoundTriggerSfxId);

	if (it == sfx.end())
	{
		activeLookSoundTriggerSfxId.clear();
		activeLookSoundVolume = 0.0f;
		return;
	}

	Sound& sound = it->second;

	float fadeOutSeconds = fmaxf(activeLookSoundFadeOutSeconds, 0.01f);
	float fadeSpeed = fmaxf(activeLookSoundVolume, 0.01f) / fadeOutSeconds;

	activeLookSoundVolume = MoveTowardsFloatLocal(
		activeLookSoundVolume,
		0.0f,
		fadeSpeed * dt
	);

	SetSoundVolume(sound, activeLookSoundVolume);

	if (activeLookSoundVolume <= 0.001f)
	{
		if (IsSoundPlaying(sound))
		{
			StopSound(sound);
		}

		// Restore this SFX's normal base volume for future non-look usage.
		auto volumeIt = sfxBaseVolumes.find(activeLookSoundTriggerSfxId);

		if (volumeIt != sfxBaseVolumes.end())
		{
			SetSoundVolume(sound, volumeIt->second);
		}

		activeLookSoundTriggerSfxId.clear();
		activeLookSoundVolume = 0.0f;
		activeLookSoundTargetVolume = 0.0f;
	}
}
void Game::StopActiveLookSoundImmediate()
{
	if (activeLookSoundTriggerSfxId.empty())
		return;

	auto it = sfx.find(activeLookSoundTriggerSfxId);

	if (it != sfx.end())
	{
		Sound& sound = it->second;

		if (IsSoundPlaying(sound))
		{
			StopSound(sound);
		}

		auto volumeIt = sfxBaseVolumes.find(activeLookSoundTriggerSfxId);

		if (volumeIt != sfxBaseVolumes.end())
		{
			SetSoundVolume(sound, volumeIt->second);
		}
	}

	activeLookSoundTriggerSfxId.clear();
	activeLookSoundVolume = 0.0f;
	activeLookSoundTargetVolume = 0.0f;
}


void Game::UpdateCinematicTriggers(float dt)
{
	if (editMode)
		return;

	if (cursorUnlocked)
		return;

	if (inspectMode)
		return;

	if (IsCinematicCameraActive())
		return;

	BoundingBox playerBox = MakePlayerCollisionBoxAt(player.m_pos);

	for (CinematicTriggerZone& trigger : cinematicTriggers)
	{
		if (!trigger.enabled)
			continue;

		BoundingBox triggerBox = GetCinematicTriggerBounds(trigger);

		bool inside = CheckCollisionBoxes(playerBox, triggerBox);

		if (!inside)
		{
			trigger.wasInside = false;
			continue;
		}

		// Only trigger on enter, not every frame while standing inside.
		if (trigger.wasInside)
			continue;

		trigger.wasInside = true;

		if (!trigger.repeatable && trigger.triggered)
			continue;

		trigger.triggered = true;

		StartCinematicLookAt(
			trigger.lookTarget,
			trigger.duration,
			trigger.lockPlayerMovement,
			trigger.syncPlayerLookAtEnd
		);

		if (trigger.playSelfDialogue && !trigger.selfDialogueLines.empty())
		{
			std::string scriptId = "__cine_" + SanitizeVoiceToken(trigger.id);

			std::string oldPrefix = trigger.selfDialogueVoicePrefix.empty()
				? trigger.id
				: trigger.selfDialogueVoicePrefix;

			std::vector<SelfDialogueLine> lines;
			lines.reserve(trigger.selfDialogueLines.size());

			for (int i = 0; i < (int)trigger.selfDialogueLines.size(); i++)
			{
				SelfDialogueLine line{};
				line.text = trigger.selfDialogueLines[i];
				line.voicePath = MakeVoicePathFromPrefix(oldPrefix, i);
				line.sfxId = "";

				lines.push_back(line);
			}

			StartSelfDialogueLines(lines, scriptId);
		}
	}
}

void Game::DrawCinematicTriggerDebug() const
{
	if (!editMode && !drawEditorPointDebug)
		return;

	for (int i = 0; i < (int)cinematicTriggers.size(); i++)
	{
		const CinematicTriggerZone& trigger = cinematicTriggers[i];

		Color color = trigger.enabled
			? Color{ 120, 180, 255, 90 }
		: Color{ 120, 120, 120, 60 };

		if (i == selectedCinematicTriggerIndex)
		{
			color = Color{ 255, 220, 80, 120 };
		}

		DrawCubeV(
			trigger.position,
			trigger.size,
			color
		);

		DrawCubeWiresV(
			trigger.position,
			trigger.size,
			i == selectedCinematicTriggerIndex ? YELLOW : SKYBLUE
		);

		DrawSphere(
			trigger.lookTarget,
			0.12f,
			i == selectedCinematicTriggerIndex ? YELLOW : GREEN
		);

		DrawLine3D(
			trigger.position,
			trigger.lookTarget,
			i == selectedCinematicTriggerIndex ? YELLOW : GREEN
		);
	}
}
void Game::ApplyCameraLookAt(Vector3 target, float dt, float speed)
{
	Vector3 currentDir = Vector3Subtract(
		camera.target,
		camera.position
	);

	if (Vector3Length(currentDir) <= 0.0001f)
	{
		currentDir = NormalizeSafe(GetCameraForward());
	}
	else
	{
		currentDir = Vector3Normalize(currentDir);
	}

	Vector3 desiredDir = Vector3Subtract(
		target,
		camera.position
	);

	if (Vector3Length(desiredDir) <= 0.0001f)
		return;

	desiredDir = Vector3Normalize(desiredDir);

	// Framerate-independent smoothing.
	float t = 1.0f - expf(-speed * dt);
	t = Clamp(t, 0.0f, 1.0f);

	Vector3 dir = Vector3Lerp(
		currentDir,
		desiredDir,
		t
	);

	if (Vector3Length(dir) <= 0.0001f)
		return;

	dir = Vector3Normalize(dir);

	camera.target = Vector3Add(
		camera.position,
		dir
	);

	camera.up = { 0.0f, 1.0f, 0.0f };
}

void Game::SyncPlayerLookFromCameraTarget()
{
	Vector3 dir = Vector3Subtract(
		camera.target,
		camera.position
	);

	if (Vector3Length(dir) <= 0.0001f)
		return;

	dir = Vector3Normalize(dir);

	Vector3 flatDir{
		dir.x,
		0.0f,
		dir.z
	};

	if (Vector3Length(flatDir) > 0.0001f)
	{
		flatDir = Vector3Normalize(flatDir);

		// Important:
		// updateCameraFPS() starts from targetOffset {0,0,-1},
		// so yaw must be derived against -Z, not +Z.
		player.yaw = atan2f(
			-flatDir.x,
			-flatDir.z
		);
	}

	// updateCameraFPS() uses pitchAngle = -player.pitch,
	// so looking upward means player.pitch should become negative.
	player.pitch = -asinf(
		Clamp(dir.y, -0.98f, 0.98f)
	);

	lean = { 0.0f, 0.0f };
	walkLerp = 0.0f;
}
void Game::StopPlayerMovementForDialogue()
{
	player.m_forward = 0;
	player.m_sideway = 0;
	player.m_jumpPressed = false;

	player.m_dir = { 0.0f, 0.0f, 0.0f };

	player.m_velocity.x = 0.0f;
	player.m_velocity.z = 0.0f;

	lean = { 0.0f, 0.0f };
	walkLerp = 0.0f;

	if (playerCharacter)
	{
		JPH::Vec3 velocity =
			playerCharacter->GetLinearVelocity();

		velocity.SetX(0.0f);
		velocity.SetZ(0.0f);

		playerCharacter->SetLinearVelocity(velocity);
	}
}

