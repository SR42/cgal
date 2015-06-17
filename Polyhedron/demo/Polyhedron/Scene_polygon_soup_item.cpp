#include <vector>
#include <queue>

#include "Scene_polygon_soup_item.h"
#include "Scene_polyhedron_item.h"
#include <CGAL/IO/Polyhedron_iostream.h>
#include <CGAL/Polyhedron_incremental_builder_3.h>

#include <QObject>
#include <QtDebug>

#include <set>
#include <stack>
#include <algorithm>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>

#include <CGAL/IO/OFF_reader.h>
#include <CGAL/IO/File_writer_OFF.h>
#include <CGAL/version.h>

#include <CGAL/orient_polygon_soup.h>
#include <CGAL/polygon_soup_to_polyhedron_3.h>
#include <CGAL/orient_polyhedron_3.h>

#include <CGAL/Triangulation_vertex_base_with_info_2.h>
#include <CGAL/Triangulation_face_base_with_info_2.h>
#include <CGAL/Constrained_Delaunay_triangulation_2.h>
#include <CGAL/Constrained_triangulation_plus_2.h>
#include <CGAL/Triangulation_2_filtered_projection_traits_3.h>

#include <CGAL/internal/Operations_on_polyhedra/compute_normal.h>



struct light_info
{
    //position
    GLfloat position[4];

    //ambient
    GLfloat ambient[4];

    //diffuse
    GLfloat diffuse[4];

    //specular
    GLfloat specular[4];
    GLfloat spec_power;

};
struct Polyhedron_to_polygon_soup_writer {
    typedef Kernel::Point_3 Point_3;

    Polygon_soup* soup;
    Polygon_soup::Polygon_3 polygon;

    Polyhedron_to_polygon_soup_writer(Polygon_soup* soup) : soup(soup), polygon() {
    }

    void write_header( std::ostream&,
                       std::size_t /* vertices */,
                       std::size_t /* halfedges */,
                       std::size_t /* facets */,
                       bool /* normals */ = false ) {
        soup->clear();
    }

    void write_footer() {
    }

    void write_vertex( const double& x, const double& y, const double& z) {
        soup->points.push_back(Point_3(x, y, z));
    }

    void write_normal( const double& /* x */, const double& /* y */, const double& /* z */) {
    }

    void write_facet_header() {
    }

    void write_facet_begin( std::size_t no) {
        polygon.clear();
        polygon.reserve(no);
    }
    void write_facet_vertex_index( std::size_t index) {
        polygon.push_back(index);
    }
    void write_facet_end() {
        soup->polygons.push_back(polygon);
        polygon.clear();
    }
}; // end struct Polyhedron_to_soup_writer

void
Scene_polygon_soup_item::initialize_buffers()
{

    //vao containing the data for the facets
    {
        rendering_program_with_light.bind();

        vaos[0].bind();
        buffers[0].bind();
        buffers[0].allocate(positions_poly.data(), positions_poly.size()*sizeof(float));
        rendering_program_with_light.enableAttributeArray("vertex");
        rendering_program_with_light.setAttributeBuffer("vertex",GL_FLOAT,0,4);
        buffers[0].release();



        buffers[1].bind();
        buffers[1].allocate(normals.data(), normals.size()*sizeof(float));
        rendering_program_with_light.enableAttributeArray("normals");
        rendering_program_with_light.setAttributeBuffer("normals",GL_FLOAT,0,3);
        buffers[1].release();

        rendering_program_with_light.release();

    }
    //vao containing the data for the edges
    {
        rendering_program_without_light.bind();
        vaos[1].bind();

        buffers[3].bind();
        buffers[3].allocate(positions_lines.data(), positions_lines.size()*sizeof(float));
        rendering_program_without_light.enableAttributeArray("vertex");
        rendering_program_without_light.setAttributeBuffer("vertex",GL_FLOAT,0,4);
        buffers[3].release();

        rendering_program_without_light.release();

        vaos[1].release();

    }
}

typedef typename Polyhedron::Traits Traits;
typedef typename Polygon_soup::Polygon_3 Facet;
typedef CGAL::Triangulation_2_filtered_projection_traits_3<Traits>   P_traits;
typedef typename Polyhedron::Halfedge_handle Halfedge_handle;
struct Face_info {
    typename Polyhedron::Halfedge_handle e[3];
    bool is_external;
};
typedef CGAL::Triangulation_vertex_base_with_info_2<Halfedge_handle,
P_traits>        Vb;
typedef CGAL::Triangulation_face_base_with_info_2<Face_info,
P_traits>          Fb1;
typedef CGAL::Constrained_triangulation_face_base_2<P_traits, Fb1>   Fb;
typedef CGAL::Triangulation_data_structure_2<Vb,Fb>                  TDS;
typedef CGAL::No_intersection_tag                                    Itag;
typedef CGAL::Constrained_Delaunay_triangulation_2<P_traits,
TDS,
Itag>             CDTbase;
typedef CGAL::Constrained_triangulation_plus_2<CDTbase>              CDT;
void
Scene_polygon_soup_item::triangulate_polygon(Polygons_iterator pit)
{
    //Computes the normal of the facet
    const Point_3& pa = soup->points[pit->at(0)];
    const Point_3& pb = soup->points[pit->at(1)];
    const Point_3& pc = soup->points[pit->at(2)];
    typename Traits::Vector_3 normal = CGAL::cross_product(pb-pa, pc -pa);
    normal = normal / std::sqrt(normal * normal);

    P_traits cdt_traits(normal);

    CDT cdt(cdt_traits);

    int it = 0;
    int it_end =pit->size();

    // Iterates the vector of facet handles
    typename CDT::Vertex_handle previous, first;
    do {

        typename CDT::Vertex_handle vh = cdt.insert(soup->points[pit->at(it)]);
        if(first == 0) {
            first = vh;
        }
        if(previous != 0 && previous != vh) {
            cdt.insert_constraint(previous, vh);
        }
        previous = vh;
    } while( ++it != it_end );

    cdt.insert_constraint(previous, first);

    // sets mark is_external
    for(typename CDT::All_faces_iterator
        pitt = cdt.all_faces_begin(),
        end = cdt.all_faces_end();
        pitt != end; ++pitt)
    {
        pitt->info().is_external = false;
    }

    //check if the facet is external or internal
    std::queue<typename CDT::Face_handle> face_queue;
    face_queue.push(cdt.infinite_vertex()->face());
    while(! face_queue.empty() ) {
        typename CDT::Face_handle fh = face_queue.front();
        face_queue.pop();
        if(fh->info().is_external) continue;
        fh->info().is_external = true;
        for(int i = 0; i <3; ++i) {
            if(!cdt.is_constrained(std::make_pair(fh, i)))
            {
                face_queue.push(fh->neighbor(i));
            }
        }
    }


    //iterates on the internal faces to add the vertices to the positions
    //and the normals to the appropriate vectors
    int count =0;
    for(typename CDT::Finite_faces_iterator
        ffit = cdt.finite_faces_begin(),
        end = cdt.finite_faces_end();
        ffit != end; ++ffit)
    {
        count ++;
        if(ffit->info().is_external)
            continue;

        positions_poly.push_back(ffit->vertex(0)->point().x());
        positions_poly.push_back(ffit->vertex(0)->point().y());
        positions_poly.push_back(ffit->vertex(0)->point().z());
        positions_poly.push_back(1.0);

        positions_poly.push_back(ffit->vertex(1)->point().x());
        positions_poly.push_back(ffit->vertex(1)->point().y());
        positions_poly.push_back(ffit->vertex(1)->point().z());
        positions_poly.push_back(1.0);

        positions_poly.push_back(ffit->vertex(2)->point().x());
        positions_poly.push_back(ffit->vertex(2)->point().y());
        positions_poly.push_back(ffit->vertex(2)->point().z());
        positions_poly.push_back(1.0);


        const Point_3& pa = soup->points[pit->at(0)];
        const Point_3& pb = soup->points[pit->at(1)];
        const Point_3& pc = soup->points[pit->at(2)];

        Kernel::Vector_3 n = CGAL::cross_product(pb-pa, pc -pa);
        n = n / std::sqrt(n * n);

        normals.push_back(n.x());
        normals.push_back(n.y());
        normals.push_back(n.z());

        normals.push_back(n.x());
        normals.push_back(n.y());
        normals.push_back(n.z());


        normals.push_back(n.x());
        normals.push_back(n.y());
        normals.push_back(n.z());


    }
}
void
Scene_polygon_soup_item::compute_normals_and_vertices(){
    //get the vertices and normals

    typedef Polygon_soup::Polygons::size_type size_type;
    positions_poly.clear();
    positions_lines.clear();
    normals.clear();
    for(Polygons_iterator it = soup->polygons.begin();
        it != soup->polygons.end(); ++it)
    {
        if(it->size()!=3)
        {
            triangulate_polygon(it);
        }
        else{

            const Point_3& pa = soup->points[it->at(0)];
            const Point_3& pb = soup->points[it->at(1)];
            const Point_3& pc = soup->points[it->at(2)];

            Kernel::Vector_3 n = CGAL::cross_product(pb-pa, pc -pa);
            n = n / std::sqrt(n * n);

            normals.push_back(n.x());
            normals.push_back(n.y());
            normals.push_back(n.z());

            normals.push_back(n.x());
            normals.push_back(n.y());
            normals.push_back(n.z());


            normals.push_back(n.x());
            normals.push_back(n.y());
            normals.push_back(n.z());


            for(size_type i = 0; i < it->size(); ++i)
            {
                const Point_3& p = soup->points[it->at(i)];
                positions_poly.push_back(p.x());
                positions_poly.push_back(p.y());
                positions_poly.push_back(p.z());
                positions_poly.push_back(1.0);
            }
        }

        //Lines
        for(size_type i = 0; i < it->size()-1; i++)
        {
            const Point_3& pa = soup->points[it->at(i)];
            const Point_3& pb = soup->points[it->at(i+1)];
            positions_lines.push_back(pa.x());
            positions_lines.push_back(pa.y());
            positions_lines.push_back(pa.z());
            positions_lines.push_back(1.0);

            positions_lines.push_back(pb.x());
            positions_lines.push_back(pb.y());
            positions_lines.push_back(pb.z());
            positions_lines.push_back(1.0);
        }
    }

}


Scene_polygon_soup_item::Scene_polygon_soup_item()
    : Scene_item(),
      soup(0),positions_poly(0),positions_lines(0), normals(0),
      oriented(false)
{
    compile_shaders();
}

Scene_polygon_soup_item::~Scene_polygon_soup_item()
{

    delete soup;
}

Scene_polygon_soup_item*
Scene_polygon_soup_item::clone() const {
    Scene_polygon_soup_item* new_soup = new Scene_polygon_soup_item();
    new_soup->soup = soup->clone();
    new_soup->oriented = oriented;
    return new_soup;
}


bool
Scene_polygon_soup_item::load(std::istream& in)
{
    if (!soup) soup=new Polygon_soup();
    else soup->clear();

    bool result = CGAL::read_OFF(in, soup->points, soup->polygons);
    emit changed();
    return result;
}

void Scene_polygon_soup_item::init_polygon_soup(std::size_t nb_pts, std::size_t nb_polygons){
    if(!soup)
        soup = new Polygon_soup;
    soup->clear();
    soup->points.reserve(nb_pts);
    soup->polygons.reserve(nb_polygons);
    oriented = false;
}

void Scene_polygon_soup_item::finalize_polygon_soup(){ soup->fill_edges(); }

#include <CGAL/IO/generic_print_polyhedron.h>
#include <iostream>

void Scene_polygon_soup_item::load(Scene_polyhedron_item* poly_item) {
    if(!poly_item) return;
    if(!poly_item->polyhedron()) return;

    if(!soup)
        soup = new Polygon_soup;

    Polyhedron_to_polygon_soup_writer writer(soup);
    CGAL::generic_print_polyhedron(std::cerr,
                                   *poly_item->polyhedron(),
                                   writer);
    emit changed();
}

void
Scene_polygon_soup_item::setDisplayNonManifoldEdges(const bool b)
{

    soup->display_non_manifold_edges = b;
    changed();
}

bool
Scene_polygon_soup_item::displayNonManifoldEdges() const {

    return soup->display_non_manifold_edges;
}

void Scene_polygon_soup_item::shuffle_orientations()
{
    for(Polygon_soup::size_type i = 0, end = soup->polygons.size();
        i < end; ++i)
    {
        if(std::rand() % 2 == 0) soup->inverse_orientation(i);
    }
    soup->fill_edges();
    changed();
}

void Scene_polygon_soup_item::inside_out()
{
    for(Polygon_soup::size_type i = 0, end = soup->polygons.size();
        i < end; ++i)
    {
        soup->inverse_orientation(i);
    }
    soup->fill_edges();
    changed();
}

bool
Scene_polygon_soup_item::orient()
{

    if(isEmpty() || this->oriented)
        return true; // nothing to do

    oriented = CGAL::orient_polygon_soup(soup->points, soup->polygons);
    return oriented;
}


bool
Scene_polygon_soup_item::save(std::ostream& out) const
{

    typedef Polygon_soup::size_type size_type;
    CGAL::File_writer_OFF writer;
    writer.write_header(out,
                        soup->points.size(),
                        0,
                        soup->polygons.size());
    for(size_type i = 0, end = soup->points.size();
        i < end; ++i)
    {
        const Point_3& p = soup->points[i];
        writer.write_vertex( p.x(), p.y(), p.z() );
    }
    writer.write_facet_header();
    for(size_type i = 0, end = soup->polygons.size();
        i < end; ++i)
    {
        const Polygon_soup::Polygon_3& polygon = soup->polygons[i];
        const size_type size = polygon.size();
        writer.write_facet_begin(size);
        for(size_type j = 0; j < size; ++j) {
            writer.write_facet_vertex_index(polygon[j]);
        }
        writer.write_facet_end();
    }
    writer.write_footer();

    return (bool) out;
}

bool
Scene_polygon_soup_item::exportAsPolyhedron(Polyhedron* out_polyhedron)
{
    orient();
    CGAL::polygon_soup_to_polyhedron_3(*out_polyhedron, soup->points, soup->polygons);

    if(out_polyhedron->size_of_vertices() > 0) {
        // Also check whether the consistent orientation is fine
        if(!CGAL::is_oriented(*out_polyhedron)) {
            out_polyhedron->inside_out();
        }
        return true;
    }
    return false;
}
QString
Scene_polygon_soup_item::toolTip() const
{

    if(!soup)
        return QString();

    return QObject::tr("<p><b>%1</b> (mode: %5, color: %6)<br />"
                       "<i>Polygons soup</i></p>"
                       "<p>Number of vertices: %2<br />"
                       "Number of polygons: %3</p>")
            .arg(this->name())
            .arg(soup->points.size())
            .arg(soup->polygons.size())
            .arg(this->renderingModeName())
            .arg(this->color().name());
}

void
Scene_polygon_soup_item::draw(Viewer_interface* viewer) const {
    if(soup == 0) return;
    //Calls the buffer info again so that it's the right one used even if
    //there are several objects drawn
    vaos[0].bind();
    attrib_buffers(viewer,0);
    //fills the arraw of colors with the current color
    GLfloat colors[4];
    qFunc.glGetFloatv(GL_CURRENT_COLOR, colors);
    QVector4D v_colors = QVector4D(colors[0],colors[1],colors[2],colors[3]);
    // tells the GPU to use the program just created
    rendering_program_with_light.bind();
    rendering_program_with_light.setAttributeValue("colors", v_colors);
    //draw the polygons
    // the third argument is the number of vec4 that will be entered
    qFunc.glDrawArrays(GL_TRIANGLES, 0, positions_poly.size()/4);
    // Clean-up
    rendering_program_with_light.release();
    vaos[0].release();

}

void
Scene_polygon_soup_item::draw_points(Viewer_interface* viewer) const {

    if(soup == 0) return;
    vaos[1].bind();
    attrib_buffers(viewer,0);
    rendering_program_without_light.bind();
    //draw the points
    qFunc.glDrawArrays(GL_POINTS, 0, positions_lines.size()/4);
    // Clean-up
    rendering_program_without_light.release();
    vaos[1].release();
}

void
Scene_polygon_soup_item::draw_edges(Viewer_interface* viewer) const {
    if(soup == 0) return;

    vaos[1].bind();
    attrib_buffers(viewer,0);
    rendering_program_without_light.bind();
    //draw the edges
    qFunc.glDrawArrays(GL_LINES, 0, positions_lines.size()/4);
    // Clean-up
    rendering_program_without_light.release();
    vaos[1].release();

}

bool
Scene_polygon_soup_item::isEmpty() const {

    return (soup == 0 || soup->points.empty());
}
void
Scene_polygon_soup_item::changed()
{
    compute_normals_and_vertices();
    initialize_buffers();
}

Scene_polygon_soup_item::Bbox
Scene_polygon_soup_item::bbox() const {

    const Point_3& p = *(soup->points.begin());
    CGAL::Bbox_3 bbox(p.x(), p.y(), p.z(), p.x(), p.y(), p.z());
    for(Polygon_soup::Points::const_iterator it = soup->points.begin();
        it != soup->points.end();
        ++it) {
        bbox = bbox + it->bbox();
    }
    return Bbox(bbox.xmin(),bbox.ymin(),bbox.zmin(),
                bbox.xmax(),bbox.ymax(),bbox.zmax());
}

void
Scene_polygon_soup_item::new_vertex(const double& x,
                                    const double& y,
                                    const double& z)
{

    soup->points.push_back(Point_3(x, y, z));
}

void
Scene_polygon_soup_item::new_triangle(const std::size_t i,
                                      const std::size_t j,
                                      const std::size_t k)
{

    Polygon_soup::Polygon_3 new_polygon(3);
    new_polygon[0] = i;
    new_polygon[1] = j;
    new_polygon[2] = k;
    soup->polygons.push_back(new_polygon);
}

#include "Scene_polygon_soup_item.moc"

// Local Variables:
// c-basic-offset: 4
// End:

