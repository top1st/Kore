#include "pch.h"
#include "VertexBufferImpl.h"
#include <Kore/Graphics/Graphics.h>
#include "Direct3D11.h"

using namespace Kore;

static void affirm(HRESULT) { }

VertexBufferImpl::VertexBufferImpl(int count) : myCount(count) {
	
}

VertexBuffer::VertexBuffer(int count, const VertexStructure& structure) : VertexBufferImpl(count) {
	myStride = 0;
	for (int i = 0; i < structure.size; ++i) {
		switch (structure.elements[i].data) {
		case Float2VertexData:
			myStride += 2 * 4;
			break;
		case Float3VertexData:
			myStride += 3 * 4;
			break;
		case Float4VertexData:
			myStride += 4 * 4;
			break;
		case ColorVertexData:
			myStride += 1 * 4;
			break;
		}
	}

	vertices = new float[myStride / 4 * myCount];

	D3D11_BUFFER_DESC bufferDesc;
	bufferDesc.Usage               = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth           = myStride * myCount;
	bufferDesc.BindFlags           = D3D11_BIND_VERTEX_BUFFER;
	bufferDesc.CPUAccessFlags      = 0;
	bufferDesc.MiscFlags           = 0;
	bufferDesc.StructureByteStride = 0;

	affirm(device->CreateBuffer(&bufferDesc, nullptr, &vb));
}

VertexBuffer::~VertexBuffer() {
	vb->Release();
	delete[] vertices;
}

float* VertexBuffer::lock() {
	return lock(0, count());
}

float* VertexBuffer::lock(int start, int count) {
	return &vertices[start * myStride / 4];
}

void VertexBuffer::unlock() {

	context->UpdateSubresource(vb, 0, nullptr, vertices, 0, 0);
}

void VertexBuffer::set() {
	UINT stride = myStride;
	UINT offset = 0;
	//if (normals) context->IASetInputLayout(normalInputLayout);
	//else context->IASetInputLayout(noNormalInputLayout);
	context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
}

int VertexBuffer::count() {
	return myCount;
}

int VertexBuffer::stride() {
	return myStride;
}