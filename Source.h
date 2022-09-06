#pragma once
#include <vector>

namespace Cheeto {

	//Floats
	float AimbotKey = VK_RBUTTON;
	float Bone = 98;
	float Smoothing = 1;
	float AE_Distance = 100;
	float A_Distance = 200;
	float E_Distance = 200;
	float A_FOV = 100;
	float SnaplineMode = 1;

	//Bools
	bool Menu = true;
	bool CircleFOV = true;
	bool A_Prediction = false;
	bool Crosshair = false;

	bool Radar = false;
	bool Lockline = true;
	bool LockTarget = true;
	bool LockDowned = false;
	bool VisualCheck = false;
	bool Aimbot = true;
	bool DynamicAimbot = false;
	bool Box = false;
	bool CornerBox = true;
	bool BoxFilled = false;
	bool HeadDot = false;
	bool Distance = true;
	bool Snaplines = true;
	bool Skeleton = true;
	bool Name = true;

	int RadarStyle = 1;

}

void DrawCornerBox(int X, int Y, int W, int H, const ImU32& color, int thickness, ImDrawList* Draw) {
	float lineW = (W / 3);
	float lineH = (H / 3);

	Draw->AddLine(ImVec2(X, Y), ImVec2(X, Y + lineH), ImGui::GetColorU32(color), thickness);
	Draw->AddLine(ImVec2(X, Y), ImVec2(X + lineW, Y), ImGui::GetColorU32(color), thickness);
	Draw->AddLine(ImVec2(X + W - lineW, Y), ImVec2(X + W, Y), ImGui::GetColorU32(color), thickness);
	Draw->AddLine(ImVec2(X + W, Y), ImVec2(X + W, Y + lineH), ImGui::GetColorU32(color), thickness);
	Draw->AddLine(ImVec2(X, Y + H - lineH), ImVec2(X, Y + H), ImGui::GetColorU32(color), thickness);
	Draw->AddLine(ImVec2(X, Y + H), ImVec2(X + lineW, Y + H), ImGui::GetColorU32(color), thickness);
	Draw->AddLine(ImVec2(X + W - lineW, Y + H), ImVec2(X + W, Y + H), ImGui::GetColorU32(color), thickness);
	Draw->AddLine(ImVec2(X + W, Y + H - lineH), ImVec2(X + W, Y + H), ImGui::GetColorU32(color), thickness);
}