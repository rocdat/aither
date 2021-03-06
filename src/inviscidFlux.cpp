/*  This file is part of aither.
    Copyright (C) 2015-17  Michael Nucci (michael.nucci@gmail.com)

    Aither is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Aither is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <cmath>  // sqrt
#include <string>
#include <memory>
#include <algorithm>  // max
#include "inviscidFlux.hpp"
#include "eos.hpp"
#include "thermodynamic.hpp"
#include "primVars.hpp"
#include "genArray.hpp"
#include "matrix.hpp"
#include "turbulence.hpp"
#include "utility.hpp"

using std::cout;
using std::endl;
using std::cerr;
using std::vector;
using std::string;
using std::max;
using std::copysign;
using std::unique_ptr;

// flux is a 3D flux in the normal direction of the given face
/*

F = [rho * vel (dot) area
     rho * vel (dot) area * velx + P * areax
     rho * vel (dot) area * vely + P * areay
     rho * vel (dot) area * velz + P * areaz
     rho * vel (dot) area * H
     rho * vel (dot) area * k
     rho * vel (dot) area * w]

rho -- density
vel -- velocity vector (3D)
area -- area vector (3D)
P -- pressure
H -- enthalpy
velx, vely, velz -- velocity components
areax, areay, areaz -- area components
k -- turbulence kinetic energy
w -- specific turbulent dissipation

Constructor is put in a private member function because identical code is
used for constructing from primative variables and conservative variables
once the conservative variables have been changed to primative variables.
The C++11 way of delegating constructors is not used because the primVars
class is not fully defined in the inviscidFlux.hpp header. This way both
constructors (primVars version and genArray version) can call this function
to avoid code duplication.
*/
void inviscidFlux::ConstructFromPrim(const primVars &state,
                                     const unique_ptr<eos> &eqnState,
                                     const unique_ptr<thermodynamic> &thermo,
                                     const vector3d<double> &normArea) {
  // state -- primative variables
  // eqnState -- equation of state
  // normArea -- unit area vector of face

  const auto vel = state.Velocity();
  const auto velNorm = vel.DotProd(normArea);

  data_[0] = state.Rho() * velNorm;
  data_[1] = state.Rho() * velNorm * vel.X() + state.P() * normArea.X();
  data_[2] = state.Rho() * velNorm * vel.Y() + state.P() * normArea.Y();
  data_[3] = state.Rho() * velNorm * vel.Z() + state.P() * normArea.Z();
  data_[4] = state.Rho() * velNorm * state.Enthalpy(eqnState, thermo);

  data_[5] = state.Rho() * velNorm * state.Tke();
  data_[6] = state.Rho() * velNorm * state.Omega();
}

inviscidFlux::inviscidFlux(const primVars &state,
                           const unique_ptr<eos> &eqnState,
                           const unique_ptr<thermodynamic> &thermo,
                           const vector3d<double> &normArea) {
  // state -- primative variables
  // eqnState -- equation of state
  // normArea -- unit area vector of face

  this->ConstructFromPrim(state, eqnState, thermo, normArea);
}

// constructor -- initialize flux from state vector using conservative variables
// flux is a 3D flux in the normal direction of the given face
inviscidFlux::inviscidFlux(const genArray &cons,
                           const unique_ptr<eos> &eqnState,
                           const unique_ptr<thermodynamic> &thermo,
                           const unique_ptr<turbModel> &turb,
                           const vector3d<double> &normArea) {
  // cons -- genArray of conserved variables
  // eqnState -- equation of state
  // turb -- turbulence model
  // normArea -- unit area vector of face

  // convert conserved variables to primative variables
  const primVars state(cons, false, eqnState, thermo, turb);

  this->ConstructFromPrim(state, eqnState, thermo, normArea);
}

/* Function to calculate inviscid flux using Roe's approximate Riemann solver.
The function takes in the primative varibles constructed
from the left and right states, an equation of state, a face area vector, and
outputs the inviscid flux as well as the maximum wave speed.
The function uses Harten's entropy fix to correct wave speeds near 0 and near
sonic.
__________________________________________
|         |       Ul|Ur        |         |
|         |         |          |         |
|   Ui-1  |   Ui    Ua  Ui+1   |   Ui+2  |
|         |         |          |         |
|         |         |          |         |
|_________|_________|__________|_________|

In the diagram above, Ul and Ur represent the reconstructed states, which for
the MUSCL scheme (1st and 2nd order) come from the stencil shown
above. Ua represents the average state at the face at which the flux will be
calculated. In this case it is a Roe average. Once the average
state has been calculated, the Roe flux can be calculated using the following
equation.

F = 1/2 * (Fl + Fr - D)

F represents the calculated Roe flux at the given face. Fl is the inviscid flux
calculated from the reconstructed state Ul. Fr is the inviscid
flux calculated from the reconstructed state Ur. D is the dissipation term which
is calculated using the Roe averaged state, as well as the
eigen values and eigen vectors resulting from the Roe linearization.

D = A * (Ur - Ul) = T * L * T^-1 * (Ur - Ul) = T * L * (Cr - Cl)

A is the linearized Roe matrix. It is equal to the convective flux jacobian
(dF/dU) calculated with the Roe averaged state. The linearization
essentially states that the flux jacobian (which is the change in flux over
change in state) mulitplied by the change in state is equal to the
change in flux. The change in flux is then added to the average of the physical
right and left fluxes (central difference). The Roe matrix, A,
can be diagonalized where T and T^-1 are the right and left eigenvectors
respectively and L is the eigenvalues. T^-1 multiplied by the change in
state results in the change in characteristic wave amplitude (Cr - Cl), or wave
strength. In its final form (right most) T represents the
characteristic waves, L represents the wave speeds, and (Cr - Cl) represents the
wave strength across the face.

*/
inviscidFlux RoeFlux(const primVars &left, const primVars &right,
                     const unique_ptr<eos> &eqnState,
                     const unique_ptr<thermodynamic> &thermo,
                     const vector3d<double> &areaNorm) {
  // left -- primative variables from left
  // right -- primative variables from right
  // eqnState -- equation of state
  // areaNorm -- norm area vector of face

  // compute Rho averaged quantities
  // Roe averaged state
  const auto roe = RoeAveragedState(left, right);

  // Roe averaged total enthalpy
  const auto hR = roe.Enthalpy(eqnState, thermo);

  // Roe averaged speed of sound
  const auto aR = roe.SoS(thermo, eqnState);

  // Roe velocity dotted with normalized area vector
  const auto velRSum = roe.Velocity().DotProd(areaNorm);

  // Delta between right and left states
  const auto delta = right - left;

  // normal velocity difference between left and right states
  const auto normVelDiff = delta.Velocity().DotProd(areaNorm);

  // calculate wave strengths (Cr - Cl)
  const double waveStrength[NUMVARS - 1] = {
    (delta.P() - roe.Rho() * aR * normVelDiff) / (2.0 * aR * aR),
    delta.Rho() - delta.P() / (aR * aR),
    (delta.P() + roe.Rho() * aR * normVelDiff) / (2.0 * aR * aR),
    roe.Rho(),
    roe.Rho() * delta.Tke() + roe.Tke() * delta.Rho() - delta.P() * roe.Tke()
    / (aR * aR),
    roe.Rho() * delta.Omega() + roe.Omega() * delta.Rho() -
    delta.P() * roe.Omega() / (aR * aR)};

  // calculate absolute value of wave speeds (L)
  double waveSpeed[NUMVARS - 1] = {
    fabs(velRSum - aR),  // left moving acoustic wave speed
    fabs(velRSum),       // entropy wave speed
    fabs(velRSum + aR),  // right moving acoustic wave speed
    fabs(velRSum),       // shear wave speed
    fabs(velRSum),       // turbulent eqn 1 (k) wave speed
    fabs(velRSum)};      // turbulent eqn 2 (omega) wave speed

  // calculate entropy fix (Harten) and adjust wave speeds if necessary
  // default setting for entropy fix to kick in
  constexpr auto entropyFix = 0.1;

  if (waveSpeed[0] < entropyFix) {
    waveSpeed[0] = 0.5 * (waveSpeed[0] * waveSpeed[0] /
                          entropyFix + entropyFix);
  }
  if (waveSpeed[2] < entropyFix) {
    waveSpeed[2] = 0.5 * (waveSpeed[2] * waveSpeed[2] /
                          entropyFix + entropyFix);
  }
  
  // calculate right eigenvectors (T)
  // calculate eigenvector due to left acoustic wave
  const genArray lAcousticEigV(1.0, roe.U() - aR * areaNorm.X(),
                               roe.V() - aR * areaNorm.Y(),
                               roe.W() - aR * areaNorm.Z(),
                               hR - aR * velRSum, roe.Tke(), roe.Omega());

  // calculate eigenvector due to entropy wave
  const genArray entropyEigV(1.0, roe.U(), roe.V(), roe.W(),
                             0.5 * roe.Velocity().MagSq(),
                             0.0, 0.0);

  // calculate eigenvector due to right acoustic wave
  const genArray rAcousticEigV(1.0, roe.U() + aR * areaNorm.X(),
                               roe.V() + aR * areaNorm.Y(),
                               roe.W() + aR * areaNorm.Z(),
                               hR + aR * velRSum, roe.Tke(), roe.Omega());

  // calculate eigenvector due to shear wave
  const genArray shearEigV(0.0,
                           delta.U() - normVelDiff * areaNorm.X(),
                           delta.V() - normVelDiff * areaNorm.Y(),
                           delta.W() - normVelDiff * areaNorm.Z(),
                           roe.Velocity().DotProd(delta.Velocity()) -
                           velRSum * normVelDiff,
                           0.0, 0.0);

  // calculate eigenvector due to turbulent equation 1
  const genArray tkeEigV(0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0);

  // calculate eigenvector due to turbulent equation 2
  const genArray omgEigV(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0);

  // calculate dissipation term ( eigenvector * wave speed * wave strength)
  genArray dissipation(0.0);
  for (auto ii = 0; ii < NUMVARS; ii++) {
    // contribution from left acoustic wave
    // contribution from entropy wave
    // contribution from right acoustic wave
    // contribution from shear wave
    // contribution from turbulent wave 1
    // contribution from turbulent wave 2
    dissipation[ii] = waveSpeed[0] * waveStrength[0] * lAcousticEigV[ii] +
        waveSpeed[1] * waveStrength[1] * entropyEigV[ii] +
        waveSpeed[2] * waveStrength[2] * rAcousticEigV[ii] +
        waveSpeed[3] * waveStrength[3] * shearEigV[ii] +
        waveSpeed[4] * waveStrength[4] * tkeEigV[ii] +
        waveSpeed[5] * waveStrength[5] * omgEigV[ii];
  }

  // calculate left/right physical flux
  inviscidFlux leftFlux(left, eqnState, thermo, areaNorm);
  inviscidFlux rightFlux(right, eqnState, thermo, areaNorm);

  // calculate numerical Roe flux
  leftFlux.RoeFlux(rightFlux, dissipation);

  return leftFlux;
}

/* Function to calculate the AUSMPW+ flux as described in "Accurate Compuations
   of Hypersonic Flows Using AUSMPW+ Scheme and Shock-Aligned Grid Technique". 
   by Kim, Kim, & Rho. AIAA 1998. Unlike the Roe scheme, this flux scheme is an
   example of flux vector splitting instead of flux difference splitting. 
   Although more dissipative, it has shown good performance in the high speed
   regime where the Roe scheme can suffer from carbuncle problems. The flux is
   split into the convective and pressure terms, which are then split based on
   the mach number and pressure. The flux discretization is shown below.

   F = M^+_l * c * F_cl + M^-_r * c * F_cr + P^+_l * P_l + P^-_r * P_r
*/
inviscidFlux AUSMFlux(const primVars &left, const primVars &right,
                     const unique_ptr<eos> &eqnState,
                     const unique_ptr<thermodynamic> &thermo,
                     const vector3d<double> &area) {
  // left -- primative variables from left
  // right -- primative variables from right
  // eqnState -- equation of state
  // thermo -- thermodynamic model
  // area -- norm area vector of face

  // calculate average specific enthalpy on face
  const auto tl = left.Temperature(eqnState);
  const auto tr = right.Temperature(eqnState);
  const auto hl = thermo->SpecEnthalpy(tl);
  const auto hr = thermo->SpecEnthalpy(tr);
  const auto h = 0.5 * (hl + hr);

  // calculate c* from Kim, Kim, Rho 1998
  const auto t = 0.5 * (tl + tr);
  const auto sosStar =
      sqrt(2.0 * h * (thermo->Gamma(t) - 1.0) / (thermo->Gamma(t) + 1.0));

  // calculate left/right mach numbers
  const auto vell = left.Velocity().DotProd(area);
  const auto velr = right.Velocity().DotProd(area);
  const auto ml = vell / sosStar;
  const auto mr = velr / sosStar;

  // calculate speed of sound on face c_1/2 from Kim, Kim, Rho 1998
  const auto vel = 0.5 * (vell + velr);
  auto sos = 0.0;
  if (vel < 0.0) {
    sos = sosStar * sosStar / std::max(vell, sosStar);
  } else if (vel > 0.0) {
    sos = sosStar * sosStar / std::max(velr, sosStar);
  }

  // calculate split mach number and pressure terms
  const auto mPlusL =
      fabs(ml) <= 1.0 ? 0.25 * pow(ml + 1.0, 2.0) : 0.5 * (ml + fabs(ml));
  const auto mMinusR =
      fabs(mr) <= 1.0 ? -0.25 * pow(mr - 1.0, 2.0) : 0.5 * (mr - fabs(mr));
  const auto pPlus = fabs(ml) <= 1.0 ? 0.25 * pow(ml + 1.0, 2.0) * (2.0 - ml)
                                     : 0.5 * (1.0 + Sign(ml));
  const auto pMinus = fabs(mr) <= 1.0 ? 0.25 * pow(mr - 1.0, 2.0) * (2.0 + mr)
                                      : 0.5 * (1.0 - Sign(mr));

  // calculate pressure weighting terms
  const auto ps = pPlus * left.P() + pMinus * right.P();
  const auto w =
      1.0 - pow(std::min(left.P() / right.P(), right.P() / left.P()), 3.0);
  const auto fl = fabs(ml) < 1.0 ? left.P() / ps - 1.0 : 0.0;
  const auto fr = fabs(mr) < 1.0 ? right.P() / ps - 1.0 : 0.0;

  // calculate final split properties
  const auto mavg = mPlusL + mMinusR;
  const auto mPlusLBar = mavg >= 0.0
                             ? mPlusL + mMinusR * ((1.0 - w) * (1.0 + fr) - fl)
                             : mPlusL * w * (1.0 + fl);
  const auto mMinusRBar =
      mavg >= 0.0 ? mMinusR * w * (1.0 + fr)
                  : mMinusR + mPlusL * ((1.0 - w) * (1.0 + fl) - fr);

  inviscidFlux ausm;
  ausm.AUSMFlux(left, right, eqnState, thermo, area, sos, mPlusLBar, mMinusRBar,
                pPlus, pMinus);
  return ausm;
}

void inviscidFlux::AUSMFlux(const primVars &left, const primVars &right,
                            const unique_ptr<eos> &eqnState,
                            const unique_ptr<thermodynamic> &thermo,
                            const vector3d<double> &area,
                            const double &sos, const double &mPlusLBar,
                            const double &mMinusRBar, const double &pPlus,
                            const double &pMinus) {
  // calculate left flux
  const auto vl = mPlusLBar * sos;
  data_[0] = left.Rho() * vl;
  data_[1] = left.Rho() * vl * left.U() + pPlus * left.P() * area.X();
  data_[2] = left.Rho() * vl * left.V() + pPlus * left.P() * area.Y();
  data_[3] = left.Rho() * vl * left.W() + pPlus * left.P() * area.Z();
  data_[4] = left.Rho() * vl * left.Enthalpy(eqnState, thermo);
  data_[5] = left.Rho() * vl * left.Tke();
  data_[6] = left.Rho() * vl * left.Omega();

  // calculate right flux (add contribution)
  const auto vr = mMinusRBar * sos;
  data_[0] += right.Rho() * vr;
  data_[1] += right.Rho() * vr * right.U() + pMinus * right.P() * area.X();
  data_[2] += right.Rho() * vr * right.V() + pMinus * right.P() * area.Y();
  data_[3] += right.Rho() * vr * right.W() + pMinus * right.P() * area.Z();
  data_[4] += right.Rho() * vr * right.Enthalpy(eqnState, thermo);
  data_[5] += right.Rho() * vr * right.Tke();
  data_[6] += right.Rho() * vr * right.Omega();
}

inviscidFlux InviscidFlux(const primVars &left, const primVars &right,
                          const unique_ptr<eos> &eqnState,
                          const unique_ptr<thermodynamic> &thermo,
                          const vector3d<double> &area, const string &flux) {
  inviscidFlux invFlux;
  if (flux == "roe") {
    invFlux = RoeFlux(left, right, eqnState, thermo, area);
  } else if (flux == "ausm") {
    invFlux = AUSMFlux(left, right, eqnState, thermo, area);
  } else {
    cerr << "ERROR: inviscid flux type " << flux << " is not recognized!"
         << endl;
    cerr << "Choose 'roe' or 'ausm'" << endl;
    exit(EXIT_FAILURE);
  }
  return invFlux;
}

inviscidFlux RusanovFlux(const primVars &left, const primVars &right,
                         const unique_ptr<eos> &eqnState,
                         const unique_ptr<thermodynamic> &thermo,
                         const vector3d<double> &areaNorm,
                         const bool &positive) {
  // left -- primative variables from left
  // right -- primative variables from right
  // eqnState -- equation of state
  // thermo -- thermodynamic model
  // areaNorm -- norm area vector of face
  // positive -- flag that is positive to add spectral radius

  // calculate maximum spectral radius
  const auto leftSpecRad =
      fabs(left.Velocity().DotProd(areaNorm)) + left.SoS(thermo, eqnState);
  const auto rightSpecRad =
      fabs(right.Velocity().DotProd(areaNorm)) + right.SoS(thermo, eqnState);
  const auto fac = positive ? -1.0 : 1.0;
  const auto specRad = fac * std::max(leftSpecRad, rightSpecRad);

  // calculate left/right physical flux
  inviscidFlux leftFlux(left, eqnState, thermo, areaNorm);
  inviscidFlux rightFlux(right, eqnState, thermo, areaNorm);

  return 0.5 * (leftFlux + rightFlux - specRad);
}


/* Member function to calculate the Roe flux, given the left and right
 * convective fluxes as well as the dissipation term.
*/
void inviscidFlux::RoeFlux(const inviscidFlux &right, const genArray &diss) {
  // right -- right convective flux
  // diss -- dissipation term

  for (auto ii = 0; ii < NUMVARS; ii++) {
    data_[ii] = 0.5 * (data_[ii] + right.data_[ii] - diss[ii]);
  }
}

// non-member functions
// -----------------------------------------------------------

// operator overload for << - allows use of cout, cerr, etc.
ostream &operator<<(ostream &os, const inviscidFlux &flux) {
  os << flux.RhoVel() << endl;
  os << flux.RhoVelU() << endl;
  os << flux.RhoVelV() << endl;
  os << flux.RhoVelW() << endl;
  os << flux.RhoVelH() << endl;
  os << flux.RhoVelK() << endl;
  os << flux.RhoVelO() << endl;
  return os;
}

// convert the inviscid flux to a genArray
genArray inviscidFlux::ConvertToGenArray() const {
  return genArray(this->RhoVel(), this->RhoVelU(), this->RhoVelV(),
                  this->RhoVelW(), this->RhoVelH(), this->RhoVelK(),
                  this->RhoVelO());
}

// function to take in the primative variables, equation of state, face area
// vector, and conservative variable update and calculate the change in the
// convective flux
genArray ConvectiveFluxUpdate(const primVars &state,
                              const primVars &stateUpdate,
                              const unique_ptr<eos> &eqnState,
                              const unique_ptr<thermodynamic> &thermo,
                              const vector3d<double> &normArea) {
  // get inviscid flux of old state
  const inviscidFlux oldFlux(state, eqnState, thermo, normArea);

  // get updated inviscid flux
  const inviscidFlux newFlux(stateUpdate, eqnState, thermo, normArea);

  // calculate difference in flux
  const auto dFlux = newFlux - oldFlux;

  return dFlux.ConvertToGenArray();
}
