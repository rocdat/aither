/*  An open source Navier-Stokes CFD solver.
    Copyright (C) 2015  Michael Nucci (michael.nucci@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <iostream>     // cout
#include <cstdlib>      // exit()
#include <algorithm>    // sort
#include <limits>       // numeric_limits
#include <vector>       // vector
#include "kdtree.hpp"

using std::cout;
using std::endl;
using std::cerr;

// constructor for kdtree
kdtree::kdtree(const vector<vector3d<double>> &points) {
  // points -- all points to search through, stored in kdtree

  nodes_ = points;
  right_ = vector<int>(points.size(), -1);
  this->BuildKdtree(0, nodes_.size(), 0);
}

// Private member functions

// private function to find the median on a given dimension, given a set
// of points
int kdtree::FindMedian(const int &start, const int &end,
                       const int &dim) {
  // start -- starting index in nodes_ to find median
  // end -- ending index in nodes_ to find median
  // dim -- dimension (x, y, z) to find median

  const int numPts = end - start;
  const int medPos = start + (numPts - 1) / 2;  // position of median

  // put median of data in position that median would be in a sorted vector
  // this is prefered to std::sort because it is O(n) instead of O(n log(n))
  // use lambda function to compare vector3d based on given dimension
  std::nth_element(nodes_.begin() + start, nodes_.begin() + medPos,
                   nodes_.begin() + end,
                   [&dim] (const vector3d<double> &p1,
                           const vector3d<double> &p2)
                   {return p1[dim] < p2[dim];});

  // return index of median
  return medPos;
}

// private function to recursively build the k-d tree
void kdtree::BuildKdtree(const int &start, const int &end,
                         const int &depth) {
  // start -- starting index in nodes_ to build tree
  // end -- ending index in nodes_ to build tree
  // depth -- depth of tree

  const int numPts = end - start;

  // recursive base case - at leaf node
  if (numPts <= binSize_) {
    return;
  }

  // determine dimension to sort/split on
  const int dim = depth % dim_;

  // find median
  int medInd = this->FindMedian(start, end, dim);

  // move median to first element
  std::swap(nodes_[start], nodes_[medInd]);
  double median = nodes_[start][dim];

  // put all elements less than or equal to the median in
  // top portion of vector, and all other elements in bottom
  // portion. This is necessary because median calculation does
  // not sort the data

  // start is at median, so use next index down
  int pTop = start + 1;
  // end is just past the last value, so use index up
  int pBot = end - 1;

  while (pTop <= pBot) {
    // if inside this conditional, point found belongs in top
    // portion of vector, so just increment index and move on
    if (nodes_[pTop][dim] <= median) {
      pTop++;
    } else {
      // if inside this conditional, point from top index
      // belongs on bottom

      // while loop is used to find a point from bottom index
      // that belongs on top
      while (nodes_[pBot][dim] > median && pTop < pBot) {
        pBot--;
      }

      // swap points so point from top belonging on bottom,
      // and point from bottom belonging on top are in correct place
      std::swap(nodes_[pTop], nodes_[pBot]);
      pBot--;
    }
  }

  // record split index as index of right branch
  right_[start] = pTop;

  // recursively build left and right branches
  this->BuildKdtree(start + 1, right_[start], depth + 1);  // left
  this->BuildKdtree(right_[start], end, depth + 1);  // right
}

/* Private member function to peform a nearest neighbor search.
The algorithm works in the following way. First it determines which
bin of the k-d tree the point to find the neighbor for would lie in
through a series of recursive calls. Once it reaches the correct bin,
it does a brute force linear search to find the closest point in 
that bin. It updates the minimum distance and neighbor accordingly.
As the recursive calls unwind up the stack, it checks to see if it is
possible that a closer node lies on the other side of a branch split.
This is done by realizing that a closer point must lie inside of a 
sphere centered at point at which the nearest neighbor is being found,
with a radius of the current minimum distance. If this sphere crosses
over the splitting coordinate at a given tree node, then the subtree
on that side must be recursively searched as well.
 */
void kdtree::NearestNeighbor(const int &start, const int &end,
                             const int &depth,
                             const vector3d<double> &pt,
                             vector3d<double> &neighbor,
                             double &minDist) const {
  // start -- starting index in nodes_ to do search on
  // end -- ending index in nodes_ to do search on
  // depth -- depth of tree node that search is on
  // pt -- point to find nearest neighbor of
  // neighbor -- current best guess for nearest neighbor
  // minDist -- current minimum distance squared found

  const int numPts = end - start;

  // recursive base case - at leaf node do linear search
  if (numPts <= binSize_) {
    for (int ii = start; ii < end; ii++) {
      double testDistance = pt.DistSq(nodes_[ii]);
      if (testDistance < minDist) {
        minDist = testDistance;
        neighbor = nodes_[ii];
      }
    }
    return;
  }

  // determine dimension of branch split
  const int dim = depth % dim_;

  // check to see if splitting node is closer than current closest
  double testDistance = pt.DistSq(nodes_[start]);
  if (testDistance < minDist) {
    minDist = testDistance;
    neighbor = nodes_[start];
  }

  // determine if point is on left or right side of split
  if (pt[dim] <= nodes_[start][dim]) {  // left side ---------------------
    // point is on left side, so recursively search left side first
    this->NearestNeighbor(start + 1, right_[start],
                          depth + 1, pt, neighbor, minDist);

    // if bounding sphere enters right partition, recursively
    // search this side of tree
    if (this->SphereInside(pt, minDist, nodes_[start], dim)) {
      this->NearestNeighbor(right_[start], end,
                            depth + 1, pt, neighbor, minDist);
    }

  } else {  // right side ------------------------------------------------
    // point is on right side, so recursively search right side first
    this->NearestNeighbor(right_[start], end,
                          depth + 1, pt, neighbor, minDist);

    // if bounding sphere enters left partition, recursively
    // search this side of tree
    if (this->SphereInside(pt, minDist, nodes_[start], dim)) {
      this->NearestNeighbor(start + 1, right_[start],
                            depth + 1, pt, neighbor, minDist);
    }
  }
}

/* Private member function to determine if the bounding sphere crosses
over the split at a given tree node. Since the splits are done at 
constant x, y, or z values, only the coordinates in one direction 
need to be compared to determine if the sphere crosses over.
*/
bool kdtree::SphereInside(const vector3d<double> &guess,
                          const double &rSq,
                          const vector3d<double> &split,
                          const int &dim) const {
  // guess -- current guess for nearest neighbor
  // rSq -- current minimum distance squared
  // split -- coordinates at splitting node
  // dim -- dimension of split

  double dist2Split =  guess[dim] - split[dim];
  dist2Split *= dist2Split;  // compare squared distances

  // if the distance to the split is greater than the current min
  // distance, the bounding sphere cannot cross the split
  return (dist2Split > rSq) ? false : true;
}

// Public member functions

// member function to perform a nearest neighbor search
double kdtree::NearestNeighbor(const vector3d<double> &pt,
                               vector3d<double> &neighbor) const {
  // pt -- point to find nearest neighbor for
  // neighbor -- variable to output coordinates of nearest neighbor

  // start with large initial guess
  double minDist = std::numeric_limits<double>::max();
  this->NearestNeighbor(0, nodes_.size(), 0, pt, neighbor, minDist);

  // return distance, not distance squared
  return sqrt(minDist);
}