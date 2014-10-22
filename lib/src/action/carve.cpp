#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>
#include "action/carve.hpp"
#include "action/smooth.hpp"
#include "action/subdivide-edge.hpp"
#include "action/unit/on.hpp"
#include "adjacent-iterator.hpp"
#include "affected-faces.hpp"
#include "carve-brush.hpp"
#include "id.hpp"
#include "intersection.hpp"
#include "partial-action/modify-winged-vertex.hpp"
#include "primitive/sphere.hpp"
#include "winged/edge.hpp"
#include "winged/face.hpp"
#include "winged/mesh.hpp"
#include "winged/vertex.hpp"

struct ActionCarve::Impl {
  ActionCarve*              self;
  ActionUnitOn <WingedMesh> actions;

  Impl (ActionCarve* s) : self (s) {}

  void runUndoBeforePostProcessing (WingedMesh& mesh) { this->actions.undo (mesh); }
  void runRedoBeforePostProcessing (WingedMesh& mesh) { this->actions.redo (mesh); }

  void run (const CarveBrush& brush) { 
    AffectedFaces domain;
    PrimSphere    sphere (brush.position (), brush.width ());
    WingedMesh&   mesh = brush.mesh ();

    mesh.intersects               (sphere, domain);
    //this->carveFaces              (brush, domain);
    this->subdivideEdges          (brush, sphere, domain);
    this->finalize                (mesh, domain);
    this->self->bufferData        (mesh);
  }

  glm::vec3 carveVertex ( const CarveBrush& brush, const glm::vec3& normal
                        , const glm::vec3& pos) const 
  {
    const float delta = brush.y (glm::distance <float> (pos, brush.position ()));
    return pos + (normal * delta);
  }

  void carveFaces (CarveBrush brush, AffectedFaces& domain) {
    WingedMesh& mesh     = brush.mesh         ();
    VertexSet   vertices = domain.toVertexSet ();

    // get average normal
    glm::vec3 avgNormal (0.0f);
    for (WingedVertex* v : vertices) {
      avgNormal = avgNormal + v->interpolatedNormal (mesh);;
    }
    avgNormal = avgNormal / float (vertices.size ());

    // write new positions
    for (WingedVertex* v : vertices) {
      const glm::vec3 newPos = this->carveVertex (brush, avgNormal, v->vector (mesh));

      this->actions.add <PAModifyWVertex> ().move (mesh, *v, newPos);
    }
  }

  void subdivideEdges (const CarveBrush& brush, const PrimSphere& sphere, AffectedFaces& domain) {
    const float maxLength    = (4.0f/3.0f) * brush.detail ();
    const float maxLengthSqr = maxLength * maxLength;

    WingedMesh&   mesh          (brush.mesh ());
    AffectedFaces thisIteration (domain);

    auto thisIterationDomain = [&] () {
      thisIteration.reset ();
      for (WingedFace* f : domain.faces ()) {
        if (IntersectionUtil::intersects (sphere, mesh, *f)) {
          thisIteration.insert (*f);
        }
      }
      ActionSubdivideEdge::extendDomain (thisIteration);
    };

    auto isSubdividable = [&] (WingedEdge& edge) -> bool {
      return edge.lengthSqr (mesh) > maxLengthSqr;
    };

    auto subdivideEdges = [&] () {
      AffectedFaces newAF;
      for (WingedEdge* e : thisIteration.edges ()) {
        if (isSubdividable (*e)) {
          this->actions.add <ActionSubdivideEdge> ().subdivideEdge (mesh, *e, newAF);
          e->vertex1Ref ().writeNormal (mesh, e->vertex1Ref ().interpolatedNormal (mesh));
        }
      }
      domain       .insert (newAF);
      domain       .commit ();
      thisIteration.insert (newAF);
      thisIteration.commit ();
    };
    auto relaxEdges = [&] () {
      for (WingedEdge* e : thisIteration.edges ()) {
        this->actions.add <ActionSubdivideEdge> ().relaxEdge (mesh, *e, domain);
      }
      domain.commit ();
    };
    auto smoothVertices = [&] () {
      this->actions.add <ActionSmooth> ().run (mesh, thisIteration.toVertexSet (), domain);
      domain.commit ();
    };

    for (int i = 0; i < 1; i++) {
      thisIterationDomain ();

      subdivideEdges ();
      relaxEdges     ();
      smoothVertices ();
    }
  }

  void finalize (WingedMesh& mesh, AffectedFaces& domain) {
    // write normals
    for (WingedVertex* v : domain.toVertexSet ()) {
      this->actions.add <PAModifyWVertex> ().writeInterpolatedNormal (mesh,*v);
    }
    // realign
    for (WingedFace* f : domain.faces ()) {
      this->self->realignFace (mesh, std::move (*f));
    }
    domain.reset ();
  }
};

DELEGATE_BIG3_SELF (ActionCarve)
DELEGATE1          (void, ActionCarve, run, const CarveBrush&)
DELEGATE1          (void, ActionCarve, runUndoBeforePostProcessing, WingedMesh&)
DELEGATE1          (void, ActionCarve, runRedoBeforePostProcessing, WingedMesh&)
