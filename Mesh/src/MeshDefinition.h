#ifndef MESHDEFINITION_H
#define MESHDEFINITION_H

#include <OpenMesh/Core/Geometry/VectorT.hh>
#include <OpenMesh/Core/Mesh/PolyMesh_ArrayKernelT.hh>
#include <OpenMesh/Core/IO/MeshIO.hh>


struct SurfaceMeshTraits : public OpenMesh::DefaultTraits
{
	typedef OpenMesh::Vec3d Point;
	typedef OpenMesh::Vec3d Normal;
	typedef OpenMesh::Vec2d TexCoord2D;

	VertexAttributes(OpenMesh::Attributes::Status | OpenMesh::Attributes::Normal| OpenMesh::Attributes::TexCoord2D);
	FaceAttributes(OpenMesh::Attributes::Status| OpenMesh::Attributes::Normal);
	EdgeAttributes(OpenMesh::Attributes::Status);
	HalfedgeAttributes(OpenMesh::Attributes::Status);

	VertexTraits
	{
	public:
		VertexT() :is_singularity_(false),order_(1) {}
		typename Refs::VertexHandle equivalent_vertex() { return equivalent_vertex_; }
		void set_equivalent_vertex(typename Refs::VertexHandle v) { equivalent_vertex_ = v; }
		bool is_singularity() { return is_singularity_; }
		int order() { return order_; }
		void set_order(int order) { order_ = order; }
		void set_singularity(bool s) { is_singularity_ = s; }
		OpenMesh::Vec2d gradient() { return gradient_; }
		void set_gradient(OpenMesh::Vec2d t) { gradient_ = t; }
		double curvature() { return curvature_; }
		double target_curvature() { return target_curvature_; }
		void set_curvature(double k) { curvature_ = k; }
		void set_target_curvature(double target_k) { target_curvature_ = target_k; }
		int reindex() { return reindex_; }
		void set_reindex(int i) { reindex_ = i; }
		double u() { return u_; }
		void set_u(double u_value) {u_ = u_value; }

	private:
		typename Refs::VertexHandle equivalent_vertex_;
		bool is_singularity_;
		int order_;
		OpenMesh::Vec2d gradient_;
		double curvature_;
		double target_curvature_;
		double u_;
		int reindex_;

	};

	HalfedgeTraits
	{
	private:
		double weight_;
		double angle_;
	public:
		HalfedgeT() : weight_(-1.) {}
		double angle() { return angle_; }
		void set_angle(double a) { angle_ = a; }
		double weight() { return weight_; }
		void set_weight(double w) { weight_ = w; }
	};

	EdgeTraits
	{
	private:
		double length_;
	public:
		EdgeT():length_(0.0) {}
		double length() { return length_; }
		void set_length(double l) { length_ = l; }

	};

	FaceTraits
	{
	private:
		
	public:
		FaceT() {};
	};
};

typedef OpenMesh::PolyMesh_ArrayKernelT<SurfaceMeshTraits> BaseSurfaceMesh;

class SurfaceMesh : public BaseSurfaceMesh
{
public:
	SurfaceMesh();
	void RequestBoundary();
	std::vector<std::vector<HalfedgeHandle>> GetBoundaries();
protected:
	std::vector<std::vector<HalfedgeHandle>> boundaries;
};


void NormalizeMesh(SurfaceMesh &mesh);

#endif