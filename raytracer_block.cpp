// [header]
// A very basic raytracer example.
// [/header]
// [compile]
// c++ -o raytracer -O3 -Wall raytracer.cpp
// [/compile]
// [ignore]
// Copyright (C) 2012  www.scratchapixel.com
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
// [/ignore]
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <vector>
#include <iostream>
#include <cassert>
#include <chrono>
#include <mpi.h>
#include <omp.h>

#if defined __linux__ || defined __APPLE__
// "Compiled for Linux
#else
// Windows doesn't define these values by default, Linux does
#define M_PI 3.141592653589793
#define INFINITY 1e8
#endif

template<typename T>
class Vec3
{
public:
    T x, y, z;
    Vec3() : x(T(0)), y(T(0)), z(T(0)) {}
    Vec3(T xx) : x(xx), y(xx), z(xx) {}
    Vec3(T xx, T yy, T zz) : x(xx), y(yy), z(zz) {}
    Vec3& normalize()
    {
        T nor2 = length2();
        if (nor2 > 0) {
            T invNor = 1 / sqrt(nor2);
            x *= invNor, y *= invNor, z *= invNor;
        }
        return *this;
    }
    Vec3<T> operator * (const T &f) const { return Vec3<T>(x * f, y * f, z * f); }
    Vec3<T> operator * (const Vec3<T> &v) const { return Vec3<T>(x * v.x, y * v.y, z * v.z); }
    T dot(const Vec3<T> &v) const { return x * v.x + y * v.y + z * v.z; }
    Vec3<T> operator - (const Vec3<T> &v) const { return Vec3<T>(x - v.x, y - v.y, z - v.z); }
    Vec3<T> operator + (const Vec3<T> &v) const { return Vec3<T>(x + v.x, y + v.y, z + v.z); }
    Vec3<T>& operator += (const Vec3<T> &v) { x += v.x, y += v.y, z += v.z; return *this; }
    Vec3<T>& operator *= (const Vec3<T> &v) { x *= v.x, y *= v.y, z *= v.z; return *this; }
    Vec3<T> operator - () const { return Vec3<T>(-x, -y, -z); }
    T length2() const { return x * x + y * y + z * z; }
    T length() const { return sqrt(length2()); }
    friend std::ostream & operator << (std::ostream &os, const Vec3<T> &v)
    {
        os << "[" << v.x << " " << v.y << " " << v.z << "]";
        return os;
    }
};

typedef Vec3<float> Vec3f;

class Sphere
{
public:
    Vec3f center;                           /// position of the sphere
    float radius, radius2;                  /// sphere radius and radius^2
    Vec3f surfaceColor, emissionColor;      /// surface color and emission (light)
    float transparency, reflection;         /// surface transparency and reflectivity
    Sphere(
        const Vec3f &c,
        const float &r,
        const Vec3f &sc,
        const float &refl = 0,
        const float &transp = 0,
        const Vec3f &ec = 0) :
        center(c), radius(r), radius2(r * r), surfaceColor(sc), emissionColor(ec),
        transparency(transp), reflection(refl)
    { /* empty */ }
    //[comment]
    // Compute a ray-sphere intersection using the geometric solution
    //[/comment]
    bool intersect(const Vec3f &rayorig, const Vec3f &raydir, float &t0, float &t1) const
    {
        Vec3f l = center - rayorig;
        float tca = l.dot(raydir);
        if (tca < 0) return false;
        float d2 = l.dot(l) - tca * tca;
        if (d2 > radius2) return false;
        float thc = sqrt(radius2 - d2);
        t0 = tca - thc;
        t1 = tca + thc;

        return true;
    }
};

//[comment]
// This variable controls the maximum recursion depth
//[/comment]
#define MAX_RAY_DEPTH 5

float mix(const float &a, const float &b, const float &mix)
{
    return b * mix + a * (1 - mix);
}

//[comment]
// This is the main trace function. It takes a ray as argument (defined by its origin
// and direction). We test if this ray intersects any of the geometry in the scene.
// If the ray intersects an object, we compute the intersection point, the normal
// at the intersection point, and shade this point using this information.
// Shading depends on the surface property (is it transparent, reflective, diffuse).
// The function returns a color for the ray. If the ray intersects an object that
// is the color of the object at the intersection point, otherwise it returns
// the background color.
//[/comment]
Vec3f trace(
    const Vec3f &rayorig,
    const Vec3f &raydir,
    const std::vector<Sphere> &spheres,
    const int &depth)
{
    //if (raydir.length() != 1) std::cerr << "Error " << raydir << std::endl;
    float tnear = INFINITY;
    const Sphere* sphere = NULL;
    // find intersection of this ray with the sphere in the scene
    for (unsigned i = 0; i < spheres.size(); ++i) {
        float t0 = INFINITY, t1 = INFINITY;
        if (spheres[i].intersect(rayorig, raydir, t0, t1)) {
            if (t0 < 0) t0 = t1;
            if (t0 < tnear) {
                tnear = t0;
                sphere = &spheres[i];
            }
        }
    }
    // if there's no intersection return black or background color
    if (!sphere) return Vec3f(2);
    Vec3f surfaceColor = 0; // color of the ray/surfaceof the object intersected by the ray
    Vec3f phit = rayorig + raydir * tnear; // point of intersection
    Vec3f nhit = phit - sphere->center; // normal at the intersection point
    nhit.normalize(); // normalize normal direction
    // If the normal and the view direction are not opposite to each other
    // reverse the normal direction. That also means we are inside the sphere so set
    // the inside bool to true. Finally reverse the sign of IdotN which we want
    // positive.
    float bias = 1e-4; // add some bias to the point from which we will be tracing
    bool inside = false;
    if (raydir.dot(nhit) > 0) nhit = -nhit, inside = true;
    if ((sphere->transparency > 0 || sphere->reflection > 0) && depth < MAX_RAY_DEPTH) {
        float facingratio = -raydir.dot(nhit);
        // change the mix value to tweak the effect
        float fresneleffect = mix(pow(1 - facingratio, 3), 1, 0.1);
        // compute reflection direction (not need to normalize because all vectors
        // are already normalized)
        Vec3f refldir = raydir - nhit * 2 * raydir.dot(nhit);
        refldir.normalize();
        Vec3f reflection = trace(phit + nhit * bias, refldir, spheres, depth + 1);
        Vec3f refraction = 0;
        // if the sphere is also transparent compute refraction ray (transmission)
        if (sphere->transparency) {
            float ior = 1.1, eta = (inside) ? ior : 1 / ior; // are we inside or outside the surface?
            float cosi = -nhit.dot(raydir);
            float k = 1 - eta * eta * (1 - cosi * cosi);
            Vec3f refrdir = raydir * eta + nhit * (eta *  cosi - sqrt(k));
            refrdir.normalize();
            refraction = trace(phit - nhit * bias, refrdir, spheres, depth + 1);
        }
        // the result is a mix of reflection and refraction (if the sphere is transparent)
        surfaceColor = (
            reflection * fresneleffect +
            refraction * (1 - fresneleffect) * sphere->transparency) * sphere->surfaceColor;
    }
    else {
        // it's a diffuse object, no need to raytrace any further
        for (unsigned i = 0; i < spheres.size(); ++i) {
            if (spheres[i].emissionColor.x > 0) {
                // this is a light
                Vec3f transmission = 1;
                Vec3f lightDirection = spheres[i].center - phit;
                lightDirection.normalize();
                for (unsigned j = 0; j < spheres.size(); ++j) {
                    if (i != j) {
                        float t0, t1;
                        if (spheres[j].intersect(phit + nhit * bias, lightDirection, t0, t1)) {
                            transmission = 0;
                            break;
                        }
                    }
                }
                surfaceColor += sphere->surfaceColor * transmission *
                std::max(float(0), nhit.dot(lightDirection)) * spheres[i].emissionColor;
            }
        }
    }

    return surfaceColor + sphere->emissionColor;
}

//[comment]
// Main rendering function. We compute a camera ray for each pixel of the image
// trace it and return a color. If the ray hits a sphere, we return the color of the
// sphere at the intersection point, else we return the background color.
//[/comment]
void render(const std::vector<Sphere> &spheres)
{
    int numtasks, rank;

    const int W = 1280;
    const int H = 1024;
    const int tailleblock =64;
    const int Total = H/tailleblock;
    Vec3f *image = new Vec3f[W * H];
    std::vector<float> im(W*H*3);
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    float invWidth = 1 / float(W), invHeight = 1 / float(H);
    float fov = 30, aspectratio = W / float(H);
    float angle = tan(M_PI * 0.5 * fov / 180.);
    // Trace rays
    MPI_Status status;
    if ( rank == 0) {
      int count_task =0;
      //std::cout << count_task << std::endl;
      for (int i = 1; i < numtasks; i++) {
          //std::cout << count_task << std::endl;
          MPI_Send(&count_task,1,MPI_INT,i,0,MPI_COMM_WORLD);
          count_task+=1;
      }
      while (count_task<Total) {
        std::vector<float> ligne(W*3*tailleblock);
        MPI_Recv(ligne.data(),3*W*tailleblock,MPI_FLOAT,MPI_ANY_SOURCE,MPI_ANY_TAG,MPI_COMM_WORLD,&status);
        /*std::cout << "ligne rescv" << status.MPI_TAG << std::endl;
        std::cout << "pros " << status.MPI_SOURCE << std::endl;
        std::cout << "count_task " << count_task << std::endl;*/
        MPI_Send(&count_task,1,MPI_INT,status.MPI_SOURCE,0,MPI_COMM_WORLD);
        count_task+=1;
        for (int j = 0; j < W*3*tailleblock; j++) {
          im[3*tailleblock*status.MPI_TAG*W+j]=ligne[j];
        }
      }

      for (int i = 0; i < numtasks-1; i++) {
        std::vector<float> ligne(W*3*tailleblock);
        MPI_Recv(ligne.data(),W*3*tailleblock,MPI_FLOAT,MPI_ANY_SOURCE,MPI_ANY_TAG,MPI_COMM_WORLD,&status);
        /*std::cout << "ligne rescv" << status.MPI_TAG << std::endl;
        std::cout << "pros " << status.MPI_SOURCE << std::endl;
        std::cout << "count_task " << count_task << std::endl;*/
        for (int j = 0; j < W*3*tailleblock; j++) {
          im[3*tailleblock*status.MPI_TAG*W+j]=ligne[j];
        }
      }

      for (int i = 1; i < numtasks; i++) {
        int fin=-1;
        MPI_Send(&fin,1,MPI_INT,i,0,MPI_COMM_WORLD);
        //std::cout << "ligne env " << fin << std::endl;
      }
      std::ofstream ofs("./untitled.ppm", std::ios::out | std::ios::binary);
      ofs << "P6\n" << W << " " << H  << "\n255\n";
      for (unsigned i = 0; i < W *H ; ++i) {
          ofs << (unsigned char)(std::min(float(1), im[3*i]) * 255) <<
                 (unsigned char)(std::min(float(1), im[3*i+1]) * 255) <<
                 (unsigned char)(std::min(float(1), im[3*i+2]) * 255);
      }
      ofs.close();
      delete [] image;

    }
    if(rank > 0)
     {
           int stop = 0;
           while(stop != -1)
           {
                 int count_task;
                 MPI_Recv(&count_task, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &status);
                 //std::cout << "ligne env " << count_task << std::endl;
                 stop = count_task;
                 if(count_task >= 0)
                 {
                       std::vector<float> ligne(W*3*tailleblock);
 #pragma omp parallel for
		       for (int y = count_task*tailleblock; y<(count_task+1)*tailleblock; y++){
                           for (int x = 0; x < W; ++x) {
                               float xx = (2 * ((x + 0.5) * invWidth) - 1) * angle * aspectratio;
                               float yy = (1 - 2 * ((y + 0.5) * invHeight)) * angle;
                               Vec3f raydir(xx, yy, -1);
                               raydir.normalize();
                               image[W*y+x] = trace(Vec3f(0), raydir, spheres, 0);
                               ligne[3*x+(y-count_task*tailleblock)*W*3]=image[W*y+x].x;
                               ligne[3*x+1+(y-count_task*tailleblock)*W*3]=image[W*y+x].y;
                               ligne[3*x+2+(y-count_task*tailleblock)*W*3]=image[W*y+x].z;
                           //std::cout << image[W*y+x].x << std::endl;
                           }
		      }

                       /*std::cout << "ligne env " << count_task << std::endl;
                       std::cout << "from " << rank << std::endl;*/
                       MPI_Send(ligne.data(), 3*W*tailleblock, MPI_FLOAT, 0, count_task, MPI_COMM_WORLD);
                 }
                 //std::cout << "ligne env " << count_task << std::endl;
           }
     }

     MPI_Finalize();

    // for (unsigned y = 0; y < height; ++y) {
    //     for (unsigned x = 0; x < width; ++x, ++pixel) {
    //         float xx = (2 * ((x + 0.5) * invWidth) - 1) * angle * aspectratio;
    //         float yy = (1 - 2 * ((y + 0.5) * invHeight)) * angle;
    //         Vec3f raydir(xx, yy, -1);
    //         raydir.normalize();
    //         *pixel = trace(Vec3f(0), raydir, spheres, 0);
    //     }
    // }
    // Save result to a PPM image (keep these flags if you compile under Windows)
    //  for (size_t i = 0; i < W; i++) {
    //      std::cout << image[i].x << std::endl;
    //  }

}

//[comment]
// In the main function, we will create the scene which is composed of 5 spheres
// and 1 light (which is also a sphere). Then, once the scene description is complete
// we render that scene, by calling the render() function.
//[/comment]
int main(int argc, char **argv)
{
    srand48(13);
    std::vector<Sphere> spheres;
    // position, radius, surface color, reflectivity, transparency, emission color
    spheres.push_back(Sphere(Vec3f( 0.0, -10004, -20), 10000, Vec3f(0.20, 0.20, 0.20), 0, 0.0));
    const int nbSpheres = 100;
    for ( int i = 0; i < nbSpheres; ++i ) {
      float x,y,z,rd,r,b,g,t;
      x = (rand()/(1.*RAND_MAX))*20.-10.;
      y = (rand()/(1.*RAND_MAX))*2.-1.;
      z = (rand()/(1.*RAND_MAX))*10.-25.;
      rd = (rand()/(1.*RAND_MAX))*0.9+0.1;
      r  = (rand()/(1.*RAND_MAX));
      g  = (rand()/(1.*RAND_MAX));
      b  = (rand()/(1.*RAND_MAX));
      t  = (rand()/(1.*RAND_MAX))*0.5;
      spheres.push_back(Sphere(Vec3f( x, y, z), rd, Vec3f(r, g, b), 1, t));
    }
    // light
    spheres.push_back(Sphere(Vec3f( 0.0,     20, -30),     3, Vec3f(0.00, 0.00, 0.00), 0, 0.0, Vec3f(3)));
    std::chrono::time_point < std::chrono::system_clock > start, end;
    start = std::chrono::system_clock::now();
    MPI_Init( &argc, &argv);
    render(spheres);
    end = std::chrono::system_clock::now();
    std::chrono::duration < double >elapsed_seconds = end - start;
    std::cout << "Temps CPU produit  :" << elapsed_seconds.count() << " secondes\n";


    return MPI_SUCCESS;
}
