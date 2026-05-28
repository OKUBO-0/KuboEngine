#pragma once
#include "Matrix4x4.h"
#include "Matrix3x3.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Vector2.h"
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include "MyMath.h"
#include <optional>
#include <span>
#include <array>


struct VertexData {

	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;

};

struct EulerTransform {
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
};

struct QuaternionTransform {
	Vector3 scale;
	Quaternion rotate;
	Vector3 translate;
};

struct Material {

	Vector4 color;
	int32_t enableLighting;
	float padding[3];
	Matrix4x4 uvTransform;
	float shiniess;
};

struct MaterialSprite
{
	Vector4 color;
	Matrix4x4 uvTransform;
};


struct TransformationMatrix
{
	Matrix4x4 WVP;
	Matrix4x4 World;
	Matrix4x4 worldInverseTranspose;

};

struct TransformationMatrixsprite
{
	Matrix4x4 WVP;
	Matrix4x4 World;
	

};

struct DirectionalLight {

	Vector4 color;//ライトの色
	Vector3 direction;//ライトの向き
	float intensity;
	int enable;
};

struct PointLight {
	
	Vector4 color;//ライトの色
	Vector3 position;//ライトの位置
	float intensity;//ライトの強さ
	float radius; //ライトの半径
	float decay; //減衰率
	int enable;
	float padding[2];
};

struct SpotLight
{
	Vector4 color; //ライトの色
	Vector3 position; //ライトの位置
	float intensity; //ライトの強さ
	Vector3 direction; //ライトの向き
	float distance; //ライトの距離
	float decay; //減衰率
	float coneAngleCos; //スポットライトの余弦
	float cosFalloffStart;
	int enable;
	float padding[2];

};

struct MaterialData {

	std::string textureFilePath;
	uint32_t textureIndex = 0;

};

struct Node {
	QuaternionTransform transform;
	Matrix4x4 localMatrix;
	std::string name;
	std::vector<Node> children;
};

struct VertexWightData {

	float weight;
	uint32_t vectorIndex;

};

struct JointWeightData {

	Matrix4x4 inverseBindPoseMatrix;
	std::vector<VertexWightData> vertexWeights;

};



struct ModelData {

	std::map<std::string, JointWeightData> skinClusterData;
	std::vector<VertexData>vertices;
	std::vector<uint32_t> indices;
	MaterialData material;
	Node rootNode;


}; 

struct CaMeraForGpu {
	Vector3 worldPosition;
};

//animation
template<typename tValue>
struct Keyframe {
	float time;
	tValue value;
};

using KeyframeVector3 = Keyframe<Vector3>;
using KeyframeQuaternion = Keyframe<Quaternion>;

struct NodeAnimation 
{
	std::vector<KeyframeVector3> translate; //平行移動
	std::vector<KeyframeQuaternion> rotate; //回転
	std::vector<KeyframeVector3> scale; //拡大縮小


};

struct Animation {
	float duration;//アニメーションの長さ
	std::map<std::string, NodeAnimation> nodeAnimations;
};

struct Joint {
	QuaternionTransform transform;       // Transform情報
	Matrix4x4 localMatrix;               // localMatrix
	Matrix4x4 skeletonSpaceMatrix;       // skeletonSpaceでの変換行列
	std::string name;                    // 名前
	std::vector<int32_t> children;       // 子JointのIndexのリスト。いなければ空
	int32_t index;                       // 自身のIndex
	std::optional<int32_t> parent;       // 親JointのIndex。いなければnull
};

struct Skeleton {
	int32_t root;                                        // RootJointのIndex
	std::map<std::string, int32_t> jointMap;             // Joint名とIndexとの辞書
	std::vector<Joint> joints;                           // 所属しているジョイント
};
