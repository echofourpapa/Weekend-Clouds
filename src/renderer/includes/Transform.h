#pragma once

#include <DirectXMath.h>

#include "types.h"

using namespace DirectX;

namespace Awesome
{
    struct Transform
    {
        inline static const XMFLOAT3 Forward = { 0, 0, 1 };
        inline static const XMFLOAT3 Up = { 0, 1, 0 };
        inline static const XMFLOAT3 Right = { 1, 0, 0 };
        inline static const XMFLOAT3 Zero = { 0, 0, 0 };

        XMFLOAT3 position = { 0,0,0 };
        XMFLOAT3 rotation = { 0,0,0 };
        XMFLOAT3 scale = { 1,1,1 };

        XMMATRIX matrix;

        void CalcMatrix();
        const XMMATRIX GetTransformationMatrix() const;
        XMFLOAT4X4 GetMatrix();

        XMFLOAT3 GetForward();
        XMFLOAT3 GetUp();
        XMFLOAT3 GetRight();

        XMMATRIX LookAt(XMFLOAT3 target, XMFLOAT3 up, float distance);
    };

    inline void Transform::CalcMatrix()
    {
        XMVECTOR vorigin = XMLoadFloat3(&Zero);

        XMVECTOR vscale = XMLoadFloat3(&scale);

        XMVECTOR vrotation = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(rotation.x),
            XMConvertToRadians(rotation.y),
            XMConvertToRadians(rotation.z));

        XMVECTOR vpositon = XMLoadFloat3(&position);

        XMVECTOR vId = XMQuaternionRotationRollPitchYaw(0, 0, 0);
        matrix = XMMatrixAffineTransformation(vscale, vorigin, vrotation, vpositon);
    }

    inline const XMMATRIX Transform::GetTransformationMatrix() const
    {
        return matrix;
    }

    inline XMFLOAT4X4 Transform::GetMatrix()
    {
        XMFLOAT4X4 matrix;
        XMStoreFloat4x4(&matrix, GetTransformationMatrix());
        return matrix;
    }

    inline XMFLOAT3 Transform::GetForward()
    {
        XMVECTOR quat = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(rotation.x), XMConvertToRadians(rotation.y), XMConvertToRadians(rotation.z));
        XMVECTOR forward = XMLoadFloat3(&Forward);

        XMVECTOR tForward = XMVector3Rotate(forward, quat);

        XMFLOAT3 rForward = {};
        XMStoreFloat3(&rForward, tForward);
        
        return rForward;
    }

    inline XMFLOAT3 Transform::GetUp()
    {
        XMVECTOR quat = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(rotation.x), XMConvertToRadians(rotation.y), XMConvertToRadians(rotation.z));
        XMVECTOR forward = XMLoadFloat3(&Up);

        XMVECTOR tForward = XMVector3Rotate(forward, quat);

        XMFLOAT3 rForward = {};
        XMStoreFloat3(&rForward, tForward);

        return rForward;
    }

    inline XMFLOAT3 Transform::GetRight()
    {
        XMVECTOR quat = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(rotation.x), XMConvertToRadians(rotation.y), XMConvertToRadians(rotation.z));
        XMVECTOR forward = XMLoadFloat3(&Right);

        XMVECTOR tForward = XMVector3Rotate(forward, quat);

        XMFLOAT3 rForward = {};
        XMStoreFloat3(&rForward, tForward);

        return rForward;
    }

    inline XMMATRIX Transform::LookAt(XMFLOAT3 target, XMFLOAT3 up, float distance)
    {
        XMVECTOR quat = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(rotation.x), XMConvertToRadians(rotation.y), XMConvertToRadians(rotation.z));
        
        XMVECTOR l_up = XMLoadFloat3(&up);
        XMVECTOR eye = XMLoadFloat3(&Forward);
        XMVECTOR look = XMLoadFloat3(&target);

        eye = XMVector3Rotate(eye, quat) * distance;
        l_up = XMVector3Rotate(l_up, quat);
        eye = look - eye;
        XMStoreFloat3(&position, eye);

        return  XMMatrixLookAtLH(eye, look, l_up);
    }
};