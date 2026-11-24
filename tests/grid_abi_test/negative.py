from pathlib import Path
import sys
kind,src,dst=sys.argv[1:];s=Path(src).read_text()
def one(a,b):
 global s
 assert s.count(a)==1,(a,s.count(a));s=s.replace(a,b)
if kind=='direct':
 one('CallThis(FUN_608fa0, g, (BYTE*)p, x);','CallStd(FUN_608fa0, (BYTE*)p, x);')
elif kind=='join':
 one('void __fastcall Grd_JoinBranches(BYTE* g, void*, BYTE* a)','void __stdcall Grd_JoinBranches(BYTE* a)')
 assert s.count('CallThis(FUN_608fa0, g, b, ')==4
 s=s.replace('CallThis(FUN_608fa0, g, b, ','CallStd(FUN_608fa0, b, ')
 one('Grd_JoinBranches(g, 0, r1);','Grd_JoinBranches(r1);')
 s+='\nKING_ALIAS("@Grd_JoinBranches@12", "_Grd_JoinBranches@4")\n'
elif kind=='join-caller':
 one('Grd_JoinBranches(g, 0, r1);','((void (__stdcall*)(BYTE*))Grd_JoinBranches)(r1);')
elif kind=='marine':
 one('void __fastcall Grid_TryJoin(BYTE* mesh, void*, BYTE* t)','void __stdcall Grid_TryJoin(BYTE* t)')
 one('GridMesh_CreateEdge(mesh, 0, ebx, eax);','CallStd(FUN_6064c0, ebx, eax);')
 one('Grid_TryJoin(m, 0, r1);','Grid_TryJoin(r1);')
elif kind=='dust':
 one('kInitialPuffScale = 0.70710677f;', 'kInitialPuffScale = 0.71f;')
elif kind=='speed':
 one('x = x < 0.0 ? -(x * x) : x * x;', 'x = x * x87_fabs(x);')
elif kind=='tyre':
 one('const double grip = Tyre_Grip(', 'const float grip = (float)Tyre_Grip(')
 one('double lateralForce = grip *', 'float lateralForce = grip *')
elif kind=='licence-recipient':
 one('Player_GrantLicences(driver, 300.0, 1);','Player_GrantLicences(h, 300.0, 1);')
elif kind=='light-distance':
 one('if (kind != 3) { // D3DLIGHT_DIRECTIONAL', 'if (kind == 3) { // D3DLIGHT_DIRECTIONAL')
elif kind=='light-lifetime':
 one('    PreserveSubmittedLight(self + 56);\n','')
 one('operator new(sizeof(HardwareLightStorage))','operator new(30600)')
elif kind=='wheel-support':
 one('mean = Wheels_Mean(w, F(int, s, 10428),', 'mean = Wheels_Mean(w, cnt,')
elif kind=='body-planes':
 one('        bool outside = false;\n', '')
 one('                outside = true;', '                best = 0;')
 one('        if (outside)\n            return 0;', '        if (*(DWORD*)&best == 0)\n            return 0;')
elif kind=='wheel-plane':
 a='double n = CallC<double>(FUN_610f00, b, (float*)Vec3_Sub((Vec3*)t, (const Vec3*)a, (const Vec3*)c));'
 assert s.count(a)==2
 s=s.replace(a,a.replace('(const Vec3*)a, (const Vec3*)c','(const Vec3*)c, (const Vec3*)a'))
elif kind=='contact-axes':
 # Restore the previous X/Z argument order for the secondary contact force.
 x='(float)((F(float, w, 0) * SF(8964) + F(float, w, 8) * SF(8988)) + F(float, w, 4) * SF(8976))'
 z='(float)((F(float, w, 8) * SF(8996) + SF(8972) * F(float, w, 0)) + F(float, w, 4) * SF(8984))'
 one(x,'HT2_TEST_PREVIOUS_CONTACT_X')
 one(z,x)
 one('HT2_TEST_PREVIOUS_CONTACT_X',z)
else:raise ValueError(kind)
Path(dst).write_text(s)
