#include "Functions.h"
#include "Auth.hpp"
#include <dwmapi.h>
#include "Source.h"
#include <sstream>
#include <string>
#include <algorithm>
#include <list>
#include "XorStr.hpp"
#include <iostream>
#include "xorstr.hpp"
#include <tlhelp32.h>
#include <fstream>
#include <filesystem>
#include <Windows.h>
#include <winioctl.h>
#include <lmcons.h>
#include <random>
#include <format>
#include "ImGui/imgui_internal.h"
#include "ImGui/imgui.h"
#include "Communication.h"
#include "Offsets.h"

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "wininet.lib")

Driver driver;
uint64_t base;
#define E
Vector3 LocalRelativeLocation;

IDirect3DDevice9Ex* p_Device = NULL;
D3DPRESENT_PARAMETERS p_Params = { NULL };

MSG Message = { NULL };
const MARGINS Margin = { -1 };

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WinProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam);
void CleanD3D();

ImFont* m_pFont;

RECT GameRect = { NULL };
D3DPRESENT_PARAMETERS d3dpp;

DWORD ScreenCenterX;
DWORD ScreenCenterY;
DWORD ScreenCenterZ;

static void xCreateWindow();
static void xInitD3d();
static void xMainLoop();
static LRESULT CALLBACK WinProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam);
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static HWND Window = NULL;
IDirect3D9Ex* p_Object = NULL;
static LPDIRECT3DDEVICE9 D3dDevice = NULL;

FTransform GetBoneIndex(DWORD_PTR mesh, int index) {
	DWORD_PTR bonearray = driver.RPM<DWORD_PTR>(mesh + Offset::BoneArray);
	if (bonearray == NULL) {
		bonearray = driver.RPM<DWORD_PTR>(mesh + Offset::BoneArray + 0x10);
	}
	return driver.RPM<FTransform>(bonearray + (index * 0x30));
}

Vector3 GetBoneWithRotation(DWORD_PTR mesh, int id) {
	FTransform bone = GetBoneIndex(mesh, id);
	FTransform ComponentToWorld = driver.RPM<FTransform>(mesh + Offset::ComponentToWorld);
	D3DMATRIX Matrix;
	Matrix = MatrixMultiplication(bone.ToMatrixWithScale(), ComponentToWorld.ToMatrixWithScale());
	return Vector3(Matrix._41, Matrix._42, Matrix._43);
}

Vector3 ProjectWorldToScreen(Vector3 WorldLocation) {
	Vector3 Screenlocation = Vector3(0, 0, 0);
	Vector3 Camera;

	auto chain69 = driver.RPM<uintptr_t>(Localplayer + W2S::chain69);
	uint64_t chain699 = driver.RPM<uintptr_t>(chain69 + 8);

	Camera.x = driver.RPM<float>(chain699 + W2S::chain699);
	Camera.y = driver.RPM<float>(Rootcomp + 0x12C);

	float test = asin(Camera.x);
	float degrees = test * (180.0 / M_PI);
	Camera.x = degrees;

	if (Camera.y < 0)
		Camera.y = 360 + Camera.y;

	D3DMATRIX tempMatrix = Matrix(Camera);
	Vector3 vAxisX, vAxisY, vAxisZ;

	vAxisX = Vector3(tempMatrix.m[0][0], tempMatrix.m[0][1], tempMatrix.m[0][2]);
	vAxisY = Vector3(tempMatrix.m[1][0], tempMatrix.m[1][1], tempMatrix.m[1][2]);
	vAxisZ = Vector3(tempMatrix.m[2][0], tempMatrix.m[2][1], tempMatrix.m[2][2]);

	uint64_t chain = driver.RPM<uint64_t>(Localplayer + W2S::chain);
	uint64_t chain1 = driver.RPM<uint64_t>(chain + W2S::chain1);
	uint64_t chain2 = driver.RPM<uint64_t>(chain1 +  W2S::chain2);

	Vector3 vDelta = WorldLocation - driver.RPM<Vector3>(chain2 + W2S::vDelta);
	Vector3 vTransformed = Vector3(vDelta.Dot(vAxisY), vDelta.Dot(vAxisZ), vDelta.Dot(vAxisX));

	if (vTransformed.z < 1.f)
		vTransformed.z = 1.f;

	zoom = driver.RPM<float>(chain699 + W2S::zoom);

	float FovAngle = 80.0f / (zoom / 1.19f);

	float ScreenCenterX = Width / 2;
	float ScreenCenterY = Height / 2;
	float ScreenCenterZ = Height / 2;

	Screenlocation.x = ScreenCenterX + vTransformed.x * (ScreenCenterX / tanf(FovAngle * (float)M_PI / 360.f)) / vTransformed.z;
	Screenlocation.y = ScreenCenterY - vTransformed.y * (ScreenCenterX / tanf(FovAngle * (float)M_PI / 360.f)) / vTransformed.z;
	Screenlocation.z = ScreenCenterZ - vTransformed.z * (ScreenCenterX / tanf(FovAngle * (float)M_PI / 360.f)) / vTransformed.z;

	return Screenlocation;
}

Vector3 AimbotCorrection(float bulletVelocity, float bulletGravity, float targetDistance, Vector3 targetPosition, Vector3 targetVelocity) {
	Vector3 recalculated = targetPosition;
	float gravity = fabs(bulletGravity);
	float time = targetDistance / fabs(bulletVelocity);
	float bulletDrop = (gravity / 250) * time * time;
	recalculated.z += bulletDrop * 120;
	recalculated.x += time * (targetVelocity.x);
	recalculated.y += time * (targetVelocity.y);
	recalculated.z += time * (targetVelocity.z);
	return recalculated;
}

void aimbot(float x, float y, float z) {
	float ScreenCenterX = (Width / 2);
	float ScreenCenterY = (Height / 2);
	float ScreenCenterZ = (Depth / 2);
	float AimSpeed = Cheeto::Smoothing;
	float TargetX = 0;
	float TargetY = 0;
	float TargetZ = 0;

	if (x != 0) {
		if (x > ScreenCenterX) {
			TargetX = -(ScreenCenterX - x);
			TargetX /= AimSpeed;
			if (TargetX + ScreenCenterX > ScreenCenterX * 2) TargetX = 0;
		}

		if (x < ScreenCenterX) {
			TargetX = x - ScreenCenterX;
			TargetX /= AimSpeed;
			if (TargetX + ScreenCenterX < 0) TargetX = 0;
		}
	}

	if (y != 0) {
		if (y > ScreenCenterY) {
			TargetY = -(ScreenCenterY - y);
			TargetY /= AimSpeed;
			if (TargetY + ScreenCenterY > ScreenCenterY * 2) TargetY = 0;
		}

		if (y < ScreenCenterY) {
			TargetY = y - ScreenCenterY;
			TargetY /= AimSpeed;
			if (TargetY + ScreenCenterY < 0) TargetY = 0;
		}
	}

	if (z != 0) {
		if (z > ScreenCenterZ) {
			TargetZ = -(ScreenCenterZ - z);
			TargetZ /= AimSpeed;
			if (TargetZ + ScreenCenterZ > ScreenCenterZ * 2) TargetZ = 0;
		}

		if (z < ScreenCenterZ) {
			TargetZ = z - ScreenCenterZ;
			TargetZ /= AimSpeed;
			if (TargetZ + ScreenCenterZ < 0) TargetZ = 0;
		}
	}

	mouse_event(MOUSEEVENTF_MOVE, static_cast<DWORD>(TargetX), static_cast<DWORD>(TargetY), NULL, NULL);
	return;
}

double GetCrossDistance(double x1, double y1, double z1, double x2, double y2, double z2) {
	return sqrt(pow((x2 - x1), 2) + pow((y2 - y1), 2));
}

typedef struct _FNlEntity {
	uint64_t Actor;
	int ID;
	uint64_t mesh;
}FNlEntity;

std::vector<FNlEntity> entityList;

void AimAt(DWORD_PTR entity) {
	uint64_t currentactormesh = driver.RPM<uint64_t>(entity + Offset::Mesh);
	auto rootHead = GetBoneWithRotation(currentactormesh, Cheeto::Bone);

	if (Cheeto::A_Prediction) {
		float distance = localactorpos.Distance(rootHead) / 250;
		uint64_t CurrentActorRootComponent = driver.RPM<uint64_t>(entity + 0x130);
		Vector3 vellocity = driver.RPM<Vector3>(CurrentActorRootComponent + Offset::Velocity);
		Vector3 Predicted = AimbotCorrection(30000, -504, distance, rootHead, vellocity);
		Vector3 rootHeadOut = ProjectWorldToScreen(Predicted);
		if (rootHeadOut.x != 0 || rootHeadOut.y != 0 || rootHeadOut.z != 0) {
			if ((GetCrossDistance(rootHeadOut.x, rootHeadOut.y, rootHeadOut.z, Width / 2, Height / 2, Depth / 2) <= Cheeto::A_FOV * 1)) {
				if (Cheeto::Lockline) {
					ImGui::GetOverlayDrawList()->AddLine(ImVec2(Width / 2, Height / 2), ImVec2(rootHeadOut.x, rootHeadOut.y), ImColor(255, 0, 0, 255), 1.0f);
				}
				aimbot(rootHeadOut.x, rootHeadOut.y, rootHeadOut.z);
			}
		}
	}
	else {
		Vector3 rootHeadOut = ProjectWorldToScreen(rootHead);
		if (rootHeadOut.x != 0 || rootHeadOut.y != 0 || rootHeadOut.z != 0) {
			if ((GetCrossDistance(rootHeadOut.x, rootHeadOut.y, rootHeadOut.z, Width / 2, Height / 2, Depth / 2) <= Cheeto::A_FOV * 1)) {
				if (Cheeto::Lockline) {
					ImGui::GetOverlayDrawList()->AddLine(ImVec2(Width / 2, Height / 2), ImVec2(rootHeadOut.x, rootHeadOut.y), ImColor(255, 0, 0, 255), 1.0f);
				}
				aimbot(rootHeadOut.x, rootHeadOut.y, rootHeadOut.z);
			}
		}
	}
}

void LocklineControl(DWORD_PTR entity) {
	uint64_t currentactormesh = driver.RPM<uint64_t>(entity + Offset::Mesh);
	auto rootHead = GetBoneWithRotation(currentactormesh, Cheeto::Bone);

	if (Cheeto::A_Prediction) {
		float distance = localactorpos.Distance(rootHead) / 250;
		uint64_t CurrentActorRootComponent = driver.RPM<uint64_t>(entity + 0x130);
		Vector3 vellocity = driver.RPM<Vector3>(CurrentActorRootComponent + Offset::Velocity);
		Vector3 Predicted = AimbotCorrection(30000, -504, distance, rootHead, vellocity);
		Vector3 rootHeadOut = ProjectWorldToScreen(Predicted);
		if (rootHeadOut.x != 0 || rootHeadOut.y != 0 || rootHeadOut.z != 0) {
			if ((GetCrossDistance(rootHeadOut.x, rootHeadOut.y, rootHeadOut.z, Width / 2, Height / 2, Depth / 2) <= Cheeto::A_FOV * 1)) {
				if (Cheeto::Lockline) {
					ImGui::GetOverlayDrawList()->AddLine(ImVec2(Width / 2, Height / 2), ImVec2(rootHeadOut.x, rootHeadOut.y), ImColor(255, 0, 0, 255), 1.0f);
				}
			}
		}
	}
	else {
		Vector3 rootHeadOut = ProjectWorldToScreen(rootHead);
		if (rootHeadOut.x != 0 || rootHeadOut.y != 0 || rootHeadOut.z != 0) {
			if ((GetCrossDistance(rootHeadOut.x, rootHeadOut.y, rootHeadOut.z, Width / 2, Height / 2, Depth / 2) <= Cheeto::A_FOV * 1)) {
				if (Cheeto::Lockline) {
					ImGui::GetOverlayDrawList()->AddLine(ImVec2(Width / 2, Height / 2), ImVec2(rootHeadOut.x, rootHeadOut.y), ImColor(255, 0, 0, 255), 1.0f);
				}
			}
		}
	}
}

void DrawSkeleton(DWORD_PTR mesh, ImDrawList* Draw, const ImU32& color, int thickness) {
	Vector3 vHeadBone = GetBoneWithRotation(mesh, 68);
	Vector3 vHip = GetBoneWithRotation(mesh, 2);
	Vector3 vNeck = GetBoneWithRotation(mesh, 67);
	Vector3 vUpperArmLeft = GetBoneWithRotation(mesh, 9);
	Vector3 vUpperArmRight = GetBoneWithRotation(mesh, 38);
	Vector3 vLeftHand = GetBoneWithRotation(mesh, 10);
	Vector3 vRightHand = GetBoneWithRotation(mesh, 39);
	Vector3 vLeftHand1 = GetBoneWithRotation(mesh, 11);
	Vector3 vRightHand1 = GetBoneWithRotation(mesh, 40);
	Vector3 vRightThigh = GetBoneWithRotation(mesh, 76);
	Vector3 vLeftThigh = GetBoneWithRotation(mesh, 69);
	Vector3 vRightCalf = GetBoneWithRotation(mesh, 77);
	Vector3 vLeftCalf = GetBoneWithRotation(mesh, 70);
	Vector3 vLeftFoot = GetBoneWithRotation(mesh, 73);
	Vector3 vRightFoot = GetBoneWithRotation(mesh, 80);
	Vector3 vHeadBoneOut = ProjectWorldToScreen(vHeadBone);
	Vector3 vHipOut = ProjectWorldToScreen(vHip);
	Vector3 vNeckOut = ProjectWorldToScreen(vNeck);
	Vector3 vUpperArmLeftOut = ProjectWorldToScreen(vUpperArmLeft);
	Vector3 vUpperArmRightOut = ProjectWorldToScreen(vUpperArmRight);
	Vector3 vLeftHandOut = ProjectWorldToScreen(vLeftHand);
	Vector3 vRightHandOut = ProjectWorldToScreen(vRightHand);
	Vector3 vLeftHandOut1 = ProjectWorldToScreen(vLeftHand1);
	Vector3 vRightHandOut1 = ProjectWorldToScreen(vRightHand1);
	Vector3 vRightThighOut = ProjectWorldToScreen(vRightThigh);
	Vector3 vLeftThighOut = ProjectWorldToScreen(vLeftThigh);
	Vector3 vRightCalfOut = ProjectWorldToScreen(vRightCalf);
	Vector3 vLeftCalfOut = ProjectWorldToScreen(vLeftCalf);
	Vector3 vLeftFootOut = ProjectWorldToScreen(vLeftFoot);
	Vector3 vRightFootOut = ProjectWorldToScreen(vRightFoot);
	Draw->AddLine(ImVec2(vHipOut.x, vHipOut.y), ImVec2(vNeckOut.x, vNeckOut.y), color, thickness);
	Draw->AddLine(ImVec2(vUpperArmLeftOut.x, vUpperArmLeftOut.y), ImVec2(vNeckOut.x, vNeckOut.y), color, thickness);
	Draw->AddLine(ImVec2(vUpperArmRightOut.x, vUpperArmRightOut.y), ImVec2(vNeckOut.x, vNeckOut.y), color, thickness);
	Draw->AddLine(ImVec2(vLeftHandOut.x, vLeftHandOut.y), ImVec2(vUpperArmLeftOut.x, vUpperArmLeftOut.y), color, thickness);
	Draw->AddLine(ImVec2(vRightHandOut.x, vRightHandOut.y), ImVec2(vUpperArmRightOut.x, vUpperArmRightOut.y), color, thickness);
	Draw->AddLine(ImVec2(vLeftHandOut.x, vLeftHandOut.y), ImVec2(vLeftHandOut1.x, vLeftHandOut1.y), color, thickness);
	Draw->AddLine(ImVec2(vRightHandOut.x, vRightHandOut.y), ImVec2(vRightHandOut1.x, vRightHandOut1.y), color, thickness);
	Draw->AddLine(ImVec2(vLeftThighOut.x, vLeftThighOut.y), ImVec2(vHipOut.x, vHipOut.y), color, thickness);
	Draw->AddLine(ImVec2(vRightThighOut.x, vRightThighOut.y), ImVec2(vHipOut.x, vHipOut.y), color, thickness);
	Draw->AddLine(ImVec2(vLeftCalfOut.x, vLeftCalfOut.y), ImVec2(vLeftThighOut.x, vLeftThighOut.y), color, thickness);
	Draw->AddLine(ImVec2(vRightCalfOut.x, vRightCalfOut.y), ImVec2(vRightThighOut.x, vRightThighOut.y), color, thickness);
	Draw->AddLine(ImVec2(vLeftFootOut.x, vLeftFootOut.y), ImVec2(vLeftCalfOut.x, vLeftCalfOut.y), color, thickness);
	Draw->AddLine(ImVec2(vRightFootOut.x, vRightFootOut.y), ImVec2(vRightCalfOut.x, vRightCalfOut.y), color, thickness);
}

void DrawRadar()
{
	ImGui::Begin(E(" RADAR "), &Cheeto::Radar, ImVec2(200, 200), 1, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
	{
		ImVec2 siz = ImGui::GetWindowSize();
		ImVec2 pos = ImGui::GetWindowPos();


		ImDrawList* windowDrawList = ImGui::GetWindowDrawList();
		windowDrawList->AddLine(ImVec2(pos.x + (siz.x / 2), pos.y + 0), ImVec2(pos.x + (siz.x / 2), pos.y + siz.y), ImColor(255, 0, 0, 255));
		windowDrawList->AddLine(ImVec2(pos.x + 0, pos.y + (siz.y / 2)), ImVec2(pos.x + siz.x, pos.y + (siz.y / 2)), ImColor(255, 0, 0, 255));

		auto entityListCopy = entityList;

		for (auto entity : entityListCopy)
		{
			uint64_t rootcomponent = driver.RPM<uint64_t>(entity.Actor + 0x138);
			if (!rootcomponent)continue;

			Vector3 Relativelocation = driver.RPM<Vector3>(rootcomponent + 0x11C);
			if (!IsVec3Valid(Relativelocation))continue;

			bool viewCheck = false;
			Vector3 EntityPos = RotatePoint(Relativelocation, LocalRelativeLocation, pos.x, pos.y, siz.x, siz.y, zoom, 2, &viewCheck);

			int s = 4;
			switch (Cheeto::RadarStyle)
			{
			case 0:
			{
				windowDrawList->AddRect(ImVec2(EntityPos.x - s, EntityPos.y - s),
					ImVec2(EntityPos.x + s, EntityPos.y + s),
					ImColor(255, 0, 0, 255));
				break;
			}
			case 1:
			{
				windowDrawList->AddRectFilled(ImVec2(EntityPos.x - s, EntityPos.y - s),
					ImVec2(EntityPos.x + s, EntityPos.y + s),
					ImColor(255, 0, 0, 255));
				break;
			}
			case 2:
			{
				windowDrawList->AddCircle(ImVec2(EntityPos.x, EntityPos.y), s, ImColor(255, 0, 0, 255));
				break;
			}
			case 3:
			{
				windowDrawList->AddCircleFilled(ImVec2(EntityPos.x, EntityPos.y), s, ImColor(255, 0, 0, 255));
				break;
			}
			default:
				break;
			}
		}
	}
	ImGui::End();
}

void Cheat() {
	ImDrawList* Draw = ImGui::GetOverlayDrawList();

	if (Cheeto::CircleFOV) {
		ImGui::GetOverlayDrawList()->AddCircle(ImVec2(ScreenCenterX, ScreenCenterY), float(Cheeto::A_FOV), ImColor(255, 0, 0, 255), 100.0f, 1.0f);
	}

	if (Cheeto::Radar) {
		DrawRadar();
	}

	auto entityListCopy = entityList;
	float closestDistance = FLT_MAX;
	DWORD_PTR closestPawn = NULL;	

	for (unsigned long i = 0; i < entityListCopy.size(); ++i) {
		FNlEntity entity = entityListCopy[i];

		uint64_t CurActorRootComponent = driver.RPM<uint64_t>(entity.Actor + 0x130);
		if (CurActorRootComponent == (uint64_t)nullptr || CurActorRootComponent == -1 || CurActorRootComponent == NULL)
			continue;

		Vector3 actorpos = driver.RPM<Vector3>(CurActorRootComponent + 0x11C);
		Vector3 actorposW2s = ProjectWorldToScreen(actorpos);

		DWORD64 otherPlayerState = driver.RPM<uint64_t>(entity.Actor + Offset::PlayerState);
		if (otherPlayerState == (uint64_t)nullptr || otherPlayerState == -1 || otherPlayerState == NULL)
			continue;

		localactorpos = driver.RPM<Vector3>(Rootcomp + 0x11C);

		Vector3 bone66 = GetBoneWithRotation(entity.mesh, 66);
		Vector3 bone0 = GetBoneWithRotation(entity.mesh, 0);

		Vector3 top = ProjectWorldToScreen(bone66);
		Vector3 aimbotspot = ProjectWorldToScreen(bone66);
		Vector3 bottom = ProjectWorldToScreen(bone0);

		Vector3 Head = ProjectWorldToScreen(Vector3(bone66.x, bone66.y, bone66.z + 15));

		float distance = localactorpos.Distance(bone66) / 100.f;
		float BoxHeight = (float)(Head.y - bottom.y);
		float CornerHeight = abs(Head.y - bottom.y);
		float CornerWidth = BoxHeight * 0.50;

		int MyTeamId = driver.RPM<int>(PlayerState + Offset::TeamIndex);
		int ActorTeamId = driver.RPM<int>(otherPlayerState + Offset::TeamIndex);
		int curactorid = driver.RPM<int>(entity.Actor + 0x18);

		if (MyTeamId != ActorTeamId) {
			if (distance < Cheeto::E_Distance) {
				// enemy

				if (Cheeto::Box) {
					if (Cheeto::BoxFilled) {
						Draw->AddRectFilled(ImVec2(Head.x - (CornerWidth / 2), Head.y), ImVec2(bottom.x + (CornerWidth / 2), bottom.y), ImColor(255, 0, 0, 127.5));
					}
					Draw->AddRect(ImVec2(Head.x - (CornerWidth / 2), Head.y), ImVec2(bottom.x + (CornerWidth / 2), bottom.y), ImColor(255, 0, 0, 255), 0.0f, 1.0f);
				}

				if (Cheeto::CornerBox) {
					DrawCornerBox(Head.x - (CornerWidth / 2), Head.y, CornerWidth, CornerHeight, ImColor(255, 0, 0, 255), 1.0f, Draw);
				}

				if (Cheeto::Snaplines) {
					if (Cheeto::SnaplineMode == 1) {
						Draw->AddLine(ImVec2(Width / 2, Height / 100), ImVec2(Head.x, Head.y), ImColor(255, 0, 0, 255), 1.0f);
					}
					if (Cheeto::SnaplineMode == 2) {
						Draw->AddLine(ImVec2(Width / 2, Height / 2), ImVec2(Head.x, Head.y), ImColor(255, 0, 0, 255), 1.0f);
					}
					if (Cheeto::SnaplineMode == 3) {
						Draw->AddLine(ImVec2(Width / 2, Height), ImVec2(bottom.x, bottom.y), ImColor(255, 0, 0, 255), 1.0f);
					}
				}

				if (Cheeto::Distance) {
					char dist[64];
					sprintf_s(dist, "%.fM", distance);
					Draw->AddText(ImVec2(bottom.x, bottom.y), ImColor(255, 0, 0, 255), dist);
				}

				if (Cheeto::HeadDot) {
					Draw->AddCircleFilled(ImVec2(Head.x, Head.y), float(BoxHeight / 25), ImColor(255, 0, 0, 127.5), 50);
				}

				if (Cheeto::Name) {
					Draw->AddText(ImVec2(Head.x, Head.y - 20), ImColor(255, 0, 0, 255), "Player");
				}

				if (Cheeto::Skeleton) {
					DrawSkeleton(entity.mesh, Draw, ImColor(255, 0, 0, 255), 1.0f);
				}

				if (Cheeto::Aimbot) {
					auto dx = aimbotspot.x - (Width / 2);
					auto dy = aimbotspot.y - (Height / 2);
					auto dz = aimbotspot.z - (Depth / 2);
					auto dist = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
					if (dist < Cheeto::A_FOV && dist < closestDistance) {
						closestDistance = dist;
						closestPawn = entity.Actor;
					}
				}
			}
		}
		else if (CurActorRootComponent != entity.Actor) {
			if (distance < Cheeto::E_Distance) {
				// team

				if (Cheeto::Box) {
					ImGui::GetOverlayDrawList()->AddRect(ImVec2(Head.x - (CornerWidth / 2.5), Head.y), ImVec2(bottom.x + (CornerWidth / 2.5), bottom.y), ImColor(255, 255, 255, 255), 0.0f, 1.0f);
				}

			}
		}
	}

	if (Cheeto::Aimbot) {
		if (closestPawn != 0) {
			if (Cheeto::Aimbot && closestPawn && GetAsyncKeyState(Cheeto::AimbotKey) < 0) {
				AimAt(closestPawn);
			}
			else {
				LocklineControl(closestPawn);
			}
		}
	}

}

void MenuProperties() {

	ImGuiStyle* style = &ImGui::GetStyle();

	ImGui::StyleColorsClassic();
	style->WindowPadding = ImVec2(8, 8);
	style->WindowRounding = 0.0f;
	style->FramePadding = ImVec2(4, 2);
	style->FrameRounding = 0.0f;
	style->ItemSpacing = ImVec2(8, 4);
	style->ItemInnerSpacing = ImVec2(4, 4);
	style->IndentSpacing = 21.0f;
	style->ScrollbarSize = 14.0f;
	style->ScrollbarRounding = 0.0f;
	style->GrabMinSize = 10.0f;
	style->GrabRounding = 0.0f;
	style->TabRounding = 0.f;
	style->ChildRounding = 0.0f;
	style->WindowBorderSize = 1.f;
	style->ChildBorderSize = 1.f;
	style->PopupBorderSize = 0.f;
	style->FrameBorderSize = 0.f;
	style->TabBorderSize = 0.f;

	style->Colors[ImGuiCol_Text] = ImColor(255, 255, 255, 255);
	style->Colors[ImGuiCol_TextDisabled] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_WindowBg] = ImColor(22, 22, 22, 255);
	style->Colors[ImGuiCol_ChildWindowBg] = ImColor(25, 25, 25, 255);
	style->Colors[ImGuiCol_PopupBg] = ImColor(37, 37, 37, 255);
	style->Colors[ImGuiCol_Border] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_BorderShadow] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_FrameBg] = ImColor(37, 37, 37, 255);
	style->Colors[ImGuiCol_FrameBgHovered] = ImColor(37, 37, 37, 255);
	style->Colors[ImGuiCol_FrameBgActive] = ImColor(37, 37, 37, 255);
	style->Colors[ImGuiCol_TitleBg] = ImColor(255, 255, 255, 255);
	style->Colors[ImGuiCol_TitleBgCollapsed] = ImColor(255, 255, 255, 255);
	style->Colors[ImGuiCol_TitleBgActive] = ImColor(255, 255, 255, 255);
	style->Colors[ImGuiCol_MenuBarBg] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_ScrollbarBg] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_ScrollbarGrab] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_ScrollbarGrabActive] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_CheckMark] = ImColor(255, 0, 0, 255);
	style->Colors[ImGuiCol_SliderGrab] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_SliderGrabActive] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_Button] = ImColor(37, 37, 37, 255);
	style->Colors[ImGuiCol_ButtonHovered] = ImColor(37, 37, 37, 255);
	style->Colors[ImGuiCol_ButtonActive] = ImColor(37, 37, 37, 255);
	style->Colors[ImGuiCol_Header] = ImColor(22, 22, 22, 255);
	style->Colors[ImGuiCol_HeaderHovered] = ImColor(22, 22, 22, 255);
	style->Colors[ImGuiCol_HeaderActive] = ImColor(22, 22, 22, 255);
	style->Colors[ImGuiCol_Column] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_ColumnHovered] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_ColumnActive] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_ResizeGrip] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_ResizeGripHovered] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_ResizeGripActive] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_PlotLines] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_PlotLinesHovered] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_PlotHistogram] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_PlotHistogramHovered] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_TextSelectedBg] = ImColor(0, 0, 0, 0);
	style->Colors[ImGuiCol_ModalWindowDarkening] = ImColor(0, 0, 0, 0);
}

void render() {

	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	static int Tab = 1;
	static int BoneOption = 0;
	static const char* BoneOptions[]{ "Head","Neck","Chest","Pelvis" };
	static int SnaplineOption = 0;
	static const char* SnaplineOptions[]{ "Top","Center","Bottom" };

	MenuProperties();
	if (Cheeto::Menu) {

		ImGui::SetNextWindowSize({ 516.f,486.f });

		ImGui::Begin(" ", 0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar);

		ImGui::SetCursorPos({ 8.f, 73.f });

		ImGuiStyle* style = &ImGui::GetStyle();
		style->ButtonTextAlign.x = 0.0f;

		if (Tab == 1) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(ImColor(25, 25, 25, 255)));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(ImColor(25, 25, 25, 255)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(ImColor(25, 25, 25, 255)));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ImColor(255, 0, 0, 255)));
			ImGui::Button("   AIMBOT", ImVec2(175.f, 35.f));
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
		}
		else {
			if (ImGui::Button("   AIMBOT", ImVec2(175.f, 35.f))) {
				Tab = 1;
			}
		}
		//
		if (Tab == 2) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(ImColor(25, 25, 25, 255)));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(ImColor(25, 25, 25, 255)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(ImColor(25, 25, 25, 255)));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ImColor(255, 0, 0, 255)));
			ImGui::Button("   PLAYER VISUALS", ImVec2(175.f, 35.f));
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
		}
		else {
			if (ImGui::Button("   PLAYER VISUALS", ImVec2(175.f, 35.f))) {
				Tab = 2;
			}
		}
		//
		if (Tab == 3) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(ImColor(25, 25, 25, 255)));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(ImColor(25, 25, 25, 255)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(ImColor(25, 25, 25, 255)));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ImColor(255, 0, 0, 255)));
			ImGui::Button("   OTHER AIMBOT", ImVec2(175.f, 35.f));
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
		}
		else {
			if (ImGui::Button("   OTHER AIMBOT", ImVec2(175.f, 35.f))) {
				Tab = 3;
			}
		}
		//
		if (Tab == 4) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(ImColor(25, 25, 25, 255)));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(ImColor(25, 25, 25, 255)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(ImColor(25, 25, 25, 255)));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ImColor(255, 0, 0, 255)));
			ImGui::Button("   MISC", ImVec2(175.f, 35.f));
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
		}
		else {
			if (ImGui::Button("   MISC", ImVec2(175.f, 35.f))) {
				Tab = 4;
			}
		}
		//
		if (Tab == 5) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(ImColor(25, 25, 25, 255)));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(ImColor(25, 25, 25, 255)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(ImColor(25, 25, 25, 255)));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(ImColor(255, 0, 0, 255)));
			ImGui::Button("   EXPLOITS", ImVec2(175.f, 35.f));
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
		}
		else {
			if (ImGui::Button("   EXPLOITS", ImVec2(175.f, 35.f))) {
				Tab = 5;
			}
		}

		style->ButtonTextAlign.x = 0.50f;

		//
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(ImColor(37, 37, 37, 255)));
		ImGui::SetCursorPos({ 8.f, 108.f });
		ImGui::BeginChild("#Filler1", { 175.f, 4.f }, true, ImGuiWindowFlags_NoScrollbar);
		ImGui::EndChild();
		ImGui::SetCursorPos({ 8.f, 147.f });
		ImGui::BeginChild("#Filler2", { 175.f, 4.f }, true, ImGuiWindowFlags_NoScrollbar);
		ImGui::EndChild();
		ImGui::SetCursorPos({ 8.f, 186.f });
		ImGui::BeginChild("#Filler3", { 175.f, 4.f }, true, ImGuiWindowFlags_NoScrollbar);
		ImGui::EndChild();
		ImGui::SetCursorPos({ 8.f, 225.f });
		ImGui::BeginChild("#Filler4", { 175.f, 4.f }, true, ImGuiWindowFlags_NoScrollbar);
		ImGui::EndChild();
		ImGui::SetCursorPos({ 8.f, 264.f });
		ImGui::BeginChild("#Filler5", { 175.f, 214.f }, true, ImGuiWindowFlags_NoScrollbar);
		ImGui::EndChild();
		ImGui::PopStyleColor();
		//

		ImGui::SetCursorPos({183.f, 8.f});
		ImGui::BeginChild("#Main", { 325.f, 470.f }, true);
		//
		if (Tab == 1) {
			ImGui::Text("Aimbot");

			ImGui::Checkbox("Aimbot", &Cheeto::Aimbot);
			ImGui::Checkbox("Dynamic Aim", &Cheeto::DynamicAimbot);
			ImGui::Checkbox("Draw Fov", &Cheeto::CircleFOV);
			ImGui::Checkbox("Lock Aimbot Target", &Cheeto::LockTarget);
			ImGui::Checkbox("Target Downed Players", &Cheeto::LockDowned);
			ImGui::Checkbox("Visibility Check [EXPERIMENTAL]", &Cheeto::VisualCheck);
			ImGui::Text("Aim Bone");
			if (ImGui::Combo(" ", &BoneOption, BoneOptions, IM_ARRAYSIZE(BoneOptions)))
			{
				if (BoneOption == 0)
				{
					Cheeto::Bone = 98;
				}
				if (BoneOption == 1)
				{
					Cheeto::Bone = 66;
				}
				if (BoneOption == 2)
				{
					Cheeto::Bone = 7;
				}
				if (BoneOption == 3)
				{
					Cheeto::Bone = 2;
				}
			}
			ImGui::Text("FOV:"); ImGui::SameLine();
			char Display1[64];
			sprintf_s(Display1, "%.f", Cheeto::A_FOV);
			ImGui::Text(Display1);
			ImGui::SliderFloat("  ", &Cheeto::A_FOV, 0, 500);
			//
			ImGui::Text("Aimbot Smoothness:"); ImGui::SameLine();
			char Display2[64];
			sprintf_s(Display2, "%.f", Cheeto::Smoothing);
			ImGui::Text(Display2);
			ImGui::SliderFloat("   ", &Cheeto::Smoothing, 1, 25);
			//
		}
		if (Tab == 2) {
			ImGui::Text("Player Visuals");
			ImGui::Text("Player ESP Max Distance:"); ImGui::SameLine();
			char Display1[64];
			sprintf_s(Display1, "%.fm", Cheeto::E_Distance);
			ImGui::Text(Display1);
			ImGui::SliderFloat(" ", &Cheeto::E_Distance, 1, 500);

			ImGui::Checkbox("Aimbot Target Line", &Cheeto::Lockline);
			ImGui::Checkbox("Box Esp", &Cheeto::Box);
			ImGui::Checkbox("Filled Box Esp [Box Esp must be on]", &Cheeto::BoxFilled);
			ImGui::Checkbox("Corner Esp", &Cheeto::CornerBox);
			ImGui::Checkbox("Line Esp ", &Cheeto::Snaplines);
			ImGui::Text("Line Esp");
			if (ImGui::Combo("  ", &SnaplineOption, SnaplineOptions, IM_ARRAYSIZE(SnaplineOptions)))
			{
				if (SnaplineOption == 0)
				{
					Cheeto::SnaplineMode = 1;
				}
				if (SnaplineOption == 1)
				{
					Cheeto::SnaplineMode = 2;
				}
				if (SnaplineOption == 2)
				{
					Cheeto::SnaplineMode = 3;
				}
			}
			ImGui::Checkbox("Distance Esp", &Cheeto::Distance);
			ImGui::Checkbox("HeadDot Esp", &Cheeto::HeadDot);
			ImGui::Checkbox("Name Esp", &Cheeto::Name);
			ImGui::Checkbox("Skeleton Esp", &Cheeto::Skeleton);
		}
		if (Tab == 3) {
			ImGui::Text("Other Visuals");
			ImGui::Checkbox("Radar Esp", &Cheeto::Radar);
		}
		if (Tab == 4) {
			ImGui::Text("Misc");
		}
		if (Tab == 5) {
			ImGui::Text("Exploits");
		}
		//
		ImGui::EndChild();
		ImGui::SetCursorPos({ 8.f, 8.f });
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(ImColor(37, 37, 37, 255)));
		ImGui::BeginChild("#Top", { 175.f, 65.f }, true);
		ImGui::PopStyleColor();
		ImGui::SetCursorPos({ 14.f, 25.f });
		ImGui::Text("Categories");
		ImGui::EndChild();

		ImGui::End();
	}

	Cheat();

	ImGui::EndFrame();
	D3dDevice->SetRenderState(D3DRS_ZENABLE, false);
	D3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, false);
	D3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, false);
	D3dDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);

	if (D3dDevice->BeginScene() >= 0) {
		ImGui::Render();
		ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
		D3dDevice->EndScene();

	}
	HRESULT result = D3dDevice->Present(NULL, NULL, NULL, NULL);

	if (result == D3DERR_DEVICELOST && D3dDevice->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) {
		ImGui_ImplDX9_InvalidateDeviceObjects();
		D3dDevice->Reset(&d3dpp);
		ImGui_ImplDX9_CreateDeviceObjects();
	}
}


void xInitD3d()
{
	if (FAILED(Direct3DCreate9Ex(D3D_SDK_VERSION, &p_Object)))
		exit(3);

	ZeroMemory(&d3dpp, sizeof(d3dpp));
	d3dpp.BackBufferWidth = Width;
	d3dpp.BackBufferHeight = Height;
	d3dpp.BackBufferFormat = D3DFMT_A8R8G8B8;
	d3dpp.MultiSampleQuality = D3DMULTISAMPLE_NONE;
	d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.EnableAutoDepthStencil = TRUE;
	d3dpp.hDeviceWindow = Window;
	d3dpp.Windowed = TRUE;

	p_Object->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, Window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &D3dDevice);

	IMGUI_CHECKVERSION();

	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Arial.ttf", 14.f);

	(void)io;

	ImGui_ImplWin32_Init(Window);
	ImGui_ImplDX9_Init(D3dDevice);

	ImGui::StyleColorsClassic();
	io.Fonts->AddFontDefault();

	p_Object->Release();
}

int width;
int height;

void drawLoop(int width, int height) {
	while (true) {
		std::vector<FNlEntity> tmpList;

		Uworld = driver.RPM<DWORD_PTR>(base + OFFSET_UWORLD);
		DWORD_PTR Gameinstance = driver.RPM<DWORD_PTR>(Uworld + Offset::GameInstance);
		DWORD_PTR LocalPlayers = driver.RPM<DWORD_PTR>(Gameinstance + Offset::LocalPlayers);
		Localplayer = driver.RPM<DWORD_PTR>(LocalPlayers);
		PlayerController = driver.RPM<DWORD_PTR>(Localplayer + Offset::PlayerController);
		
		LocalPawn = driver.RPM<DWORD_PTR>(PlayerController + Offset::LocalPawn);

		PlayerState = driver.RPM<DWORD_PTR>(LocalPawn + Offset::PlayerState);
		Rootcomp = driver.RPM<DWORD_PTR>(LocalPawn + Offset::RootComponent);
		LocalRelativeLocation = driver.RPM<Vector3>(Rootcomp + 0x11C);

		if (LocalPawn != 0) {
			localplayerID = driver.RPM<int>(LocalPawn + 0x18);
		}

		Persistentlevel = driver.RPM<DWORD_PTR>(Uworld + Offset::Persistentlevel);

		DWORD ActorCount = driver.RPM<DWORD>(Persistentlevel + Offset::AcotrCount);
		DWORD_PTR AActors = driver.RPM<DWORD_PTR>(Persistentlevel + Offset::AAcotrs);

		for (int i = 0; i < ActorCount; i++) {
			uint64_t CurrentActor = driver.RPM<uint64_t>(AActors + i * Offset::CurrentActor);

			int curactorid = driver.RPM<int>(CurrentActor + 0x18);

			if (curactorid == localplayerID || curactorid == localplayerID + 765) {
				FNlEntity fnlEntity{ };
				fnlEntity.Actor = CurrentActor;
				fnlEntity.mesh = driver.RPM<uint64_t>(CurrentActor + Offset::Mesh);
				fnlEntity.ID = curactorid;
				tmpList.push_back(fnlEntity);
			}
		}

		entityList = tmpList;
		Sleep(2);
	}
}

uint32_t pp_id = 0;

void main() {

	while (hwnd == NULL)
	{
		Sleep(10);
		hwnd = FindWindowA(0, XorStr("LimeWare").c_str());
	}
	GetWindowThreadProcessId(hwnd, &processID);
	
	width = GetSystemMetrics(0);
	height = GetSystemMetrics(1);

	pp_id = get_process_id("FortniteClient-Win64-Shipping.exe");

	driver.Init(pp_id);
	base = driver.GetProcessBase(pp_id);	

	xCreateWindow();
	xInitD3d();

	HANDLE handle = CreateThread(nullptr, NULL, reinterpret_cast<LPTHREAD_START_ROUTINE>(drawLoop), nullptr, NULL, nullptr);
	auto pStartupInfo = new STARTUPINFOA();
	auto remoteProcessInfo = new PROCESS_INFORMATION();

	xMainLoop();
	CleanD3D();
}

void SetWindowToTarget()
{
	while (true)
	{
		if (hwnd)
		{
			ZeroMemory(&GameRect, sizeof(GameRect));
			GetWindowRect(hwnd, &GameRect);
			Width = GameRect.right - GameRect.left;
			Height = GameRect.bottom - GameRect.top;
			DWORD dwStyle = GetWindowLong(hwnd, GWL_STYLE);

			if (dwStyle & WS_BORDER)
			{
				GameRect.top += 32;
				Height -= 39;
			}
			ScreenCenterX = Width / 2;
			ScreenCenterY = Height / 2;
			MoveWindow(Window, GameRect.left, GameRect.top, Width, Height, true);
		}
		else
		{
			exit(0);
		}
	}
}

void xCreateWindow()
{
	CreateThread(0, 0, (LPTHREAD_START_ROUTINE)SetWindowToTarget, 0, 0, 0);

	WNDCLASSEX wc;
	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.lpszClassName = "conhost";
	wc.lpfnWndProc = WinProc;
	RegisterClassEx(&wc);

	if (hwnd)
	{
		GetClientRect(hwnd, &GameRect);
		POINT xy;
		ClientToScreen(hwnd, &xy);
		GameRect.left = xy.x;
		GameRect.top = xy.y;

		Width = GameRect.right;
		Height = GameRect.bottom;
	}
	else
		exit(2);

	Window = CreateWindowEx(NULL, "conhost", "conhost1", WS_POPUP | WS_VISIBLE, 0, 0, Width, Height, 0, 0, 0, 0);

	DwmExtendFrameIntoClientArea(Window, &Margin);
	SetWindowLong(Window, GWL_EXSTYLE, WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_LAYERED);
	ShowWindow(Window, SW_SHOW);
	UpdateWindow(Window);
}

void xMainLoop()
{
	static RECT old_rc;
	ZeroMemory(&Message, sizeof(MSG));

	while (Message.message != WM_QUIT)
	{
		if (GetAsyncKeyState(VK_INSERT) & 1) {
			Cheeto::Menu = !Cheeto::Menu;
		}

		if (PeekMessage(&Message, Window, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&Message);
			DispatchMessage(&Message);
		}

		HWND hwnd_active = GetForegroundWindow();

		if (hwnd_active == hwnd) {
			HWND hwndtest = GetWindow(hwnd_active, GW_HWNDPREV);
			SetWindowPos(Window, hwndtest, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		}

		if (GetAsyncKeyState(0x23) & 1)
			exit(8);

		RECT rc;
		POINT xy;

		ZeroMemory(&rc, sizeof(RECT));
		ZeroMemory(&xy, sizeof(POINT));
		GetClientRect(hwnd, &rc);
		ClientToScreen(hwnd, &xy);
		rc.left = xy.x;
		rc.top = xy.y;

		ImGuiIO& io = ImGui::GetIO();
		io.ImeWindowHandle = hwnd;
		io.DeltaTime = 1.0f / 60.0f;

		POINT p;
		GetCursorPos(&p);
		io.MousePos.x = p.x - xy.x;
		io.MousePos.y = p.y - xy.y;

		if (GetAsyncKeyState(VK_LBUTTON)) {
			io.MouseDown[0] = true;
			io.MouseClicked[0] = true;
			io.MouseClickedPos[0].x = io.MousePos.x;
			io.MouseClickedPos[0].x = io.MousePos.y;
		}
		else
			io.MouseDown[0] = false;

		if (rc.left != old_rc.left || rc.right != old_rc.right || rc.top != old_rc.top || rc.bottom != old_rc.bottom)
		{
			old_rc = rc;

			Width = rc.right;
			Height = rc.bottom;

			d3dpp.BackBufferWidth = Width;
			d3dpp.BackBufferHeight = Height;
			SetWindowPos(Window, (HWND)0, xy.x, xy.y, Width, Height, SWP_NOREDRAW);
			D3dDevice->Reset(&d3dpp);
		}
		render();
	}
	ImGui_ImplDX9_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	CleanD3D();
	DestroyWindow(Window);
}

LRESULT CALLBACK WinProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{ 


	if (ImGui_ImplWin32_WndProcHandler(hWnd, Message, wParam, lParam)) {
		return true;
	}

	switch (Message)
	{
	case WM_DESTROY:
		CleanD3D();
		PostQuitMessage(0);
		exit(4);
		break;
	case WM_SIZE:
		if (p_Device != NULL && wParam != SIZE_MINIMIZED)
		{
			ImGui_ImplDX9_InvalidateDeviceObjects();
			p_Params.BackBufferWidth = LOWORD(lParam);
			p_Params.BackBufferHeight = HIWORD(lParam);
			HRESULT hr = p_Device->Reset(&p_Params);

			if (hr == D3DERR_INVALIDCALL) {
				IM_ASSERT(0);
			}

			ImGui_ImplDX9_CreateDeviceObjects();
		}
		break;
	default:
		return DefWindowProc(hWnd, Message, wParam, lParam);
		break;
	}

	return 0;
}

void CleanD3D() {
	if (p_Device != NULL)
	{
		p_Device->EndScene();
		p_Device->Release();
	}

	if (p_Object != NULL)
	{
		p_Object->Release();
	}
}