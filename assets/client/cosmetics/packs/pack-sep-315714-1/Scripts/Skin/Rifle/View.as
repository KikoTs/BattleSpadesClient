/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

 namespace spades {
 
	class ViewRifleSkin:
	IToolSkin, IViewToolSkin, IWeaponSkin,
	BasicViewWeapon {
	
		private AudioDevice@ audioDevice;
		private Model@ gunModel;
		private Model@ gunModel2;
		private Model@ magazineModel;
		private Model@ barrel;
		private Model@ barrelhandle;
		private Model@ scope;
		private Model@ hammer;
		private Model@ dot;
		private Model@ singleVoxel;
		private Model@ bulletcyl;
		private Model@ casing1;
		private Model@ casing2;
		private Model@ casing3;
		private Model@ casing4;
		private Model@ casing5;
		private Model@ casing6;
		private Model@ casing7;
		private Model@ casing8;
		private Model@ casing9;
		private Model@ casing10;
		
		private Image@ cross;

		private AudioChunk@ fireSound;
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@ fireSmallReverbSound;
		private AudioChunk@ fireLargeReverbSound;
		private AudioChunk@ reloadSound;
		private ConfigItem@ cg_fov = ConfigItem("cg_fov");
		
		
		// pivot of the weapon when viewed in slab6
		private Vector3 pivot = Vector3(3.50, 33.0, 23.0);
		// scale of the weapon
		private float globalScale = 0.01;
		// scale of the magazine, relative to the global scale
		private float magazineScale = 0.5;
		// delta time between each frame
		private float deltatime = 0.0;
		// checks if mag has been thrown
		// reset to false whenever reloading
		private bool hasThrownMag = false;
		
		// Creates a rotation matrix from euler angles (in the form of a Vector3) x-y-z
		Matrix4 CreateEulerAnglesMatrix( Vector3 angles ) {
			Matrix4 mat = CreateRotateMatrix( Vector3(1.0, 0.0, 0.0), angles.x );
			mat = CreateRotateMatrix( Vector3(0.0, 1.0, 0.0), angles.y ) * mat;
			mat = CreateRotateMatrix( Vector3(0.0, 0.0, 1.0), angles.z ) * mat;
			
			return mat;
		}
	
		// select easing functions
		float quadraticIn(float per) {
			return (per*per);
		}
		
		float quadraticOut(float per) {
			return -(per * (per-2));
		}
		
		float quadraticInOut(float per) {
			per < per/2; 
			return (per*per);
			
			per > per/2; 
			return -(per * (per-2));
			
		}	
		
		float cubicIn(float per) {
			return (per*per*per);
		}
		
		float cubicOut(float per) {
			per = per-1;
			return (per*per*per + 1);
		}

		ViewRifleSkin(Renderer@ r, AudioDevice@ dev){
			super(r);
			@audioDevice = dev;
			@gunModel = renderer.RegisterModel
				("Models/Weapons/Rifle/WeaponNoMagazine.kv6");
				@gunModel2 = renderer.RegisterModel
				("Models/Weapons/Rifle/WeaponNoMagazine2.kv6");
			@magazineModel = renderer.RegisterModel
				("Models/Weapons/Rifle/bulletchamber.kv6");
			@barrel = renderer.RegisterModel
				("Models/Weapons/Rifle/barrel.kv6");
			@barrelhandle = renderer.RegisterModel
				("Models/Weapons/Rifle/barrelhandle.kv6");
			@scope = renderer.RegisterModel
				("Models/Weapons/Rifle/Scope.kv6");
			@hammer = renderer.RegisterModel
				("Models/Weapons/Rifle/hammer.kv6");
			@dot = renderer.RegisterModel
				("Models/Weapons/Rifle/scopeDot.kv6");
			@singleVoxel = renderer.RegisterModel
				("Models/Weapons/Rifle/SingleVoxel.kv6");
			@bulletcyl = renderer.RegisterModel
				("Models/Weapons/Rifle/bulletcyl.kv6");
			@casing1 = renderer.RegisterModel
				("Models/Weapons/Rifle/casing1.kv6");
			@casing2 = renderer.RegisterModel
				("Models/Weapons/Rifle/casing2.kv6");
			@casing3 = renderer.RegisterModel
				("Models/Weapons/Rifle/casing3.kv6");
			@casing4 = renderer.RegisterModel
				("Models/Weapons/Rifle/casing4.kv6");
			@casing5 = renderer.RegisterModel
				("Models/Weapons/Rifle/casing5.kv6");
			@casing6 = renderer.RegisterModel
				("Models/Weapons/Rifle/casing6.kv6");
			@casing7 = renderer.RegisterModel
				("Models/Weapons/Rifle/casing7.kv6");
			@casing8 = renderer.RegisterModel
				("Models/Weapons/Rifle/casing8.kv6");
			@casing9 = renderer.RegisterModel
				("Models/Weapons/Rifle/casing9.kv6");
			@casing10 = renderer.RegisterModel
				("Models/Weapons/Rifle/casing10.kv6");
				
			@cross = renderer.RegisterImage
				("Gfx/scopecross.png"); 

			@fireSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/FireLocal.wav");
			@fireFarSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/FireFar.opus");
			@fireStereoSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/FireStereo.opus");
			@reloadSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/ReloadLocal.wav");

			@fireSmallReverbSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/V2AmbienceSmall.opus");
			@fireLargeReverbSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/V2AmbienceLarge.opus");
		}

		void Update(float dt) {
			BasicViewWeapon::Update(dt);
		}

		void WeaponFired(){
			BasicViewWeapon::WeaponFired();

			if(!IsMuted){
				Vector3 origin = Vector3(0.4f, -0.3f, 0.5f);
				AudioParam param;
				param.volume = 5.f;
				audioDevice.PlayLocal(fireSound, origin, param);

				param.volume = 1.f * environmentRoom;
				if (environmentSize < 0.5f) {
					audioDevice.PlayLocal(fireSmallReverbSound, origin, param);
				} else {
					audioDevice.PlayLocal(fireLargeReverbSound, origin, param);
				}

				param.referenceDistance = 4.f;
				param.volume = 1.f;
				audioDevice.PlayLocal(fireFarSound, origin, param);
				param.referenceDistance = 1.f;
				audioDevice.PlayLocal(fireStereoSound, origin, param);
			}
		}

		void ReloadingWeapon() {
			if(!IsMuted){
				Vector3 origin = Vector3(0.4f, -0.3f, 0.5f);
				AudioParam param;
				param.volume = 20.f;
				audioDevice.PlayLocal(reloadSound, origin, param);
			}
		}

		float GetZPos() {
			return 0.2f - AimDownSightStateSmooth * 0.0520f;
		}

		// rotates gun matrix to ensure the sight is in
		// the center of screen (0, ?, 0).
		Matrix4 AdjustToAlignSight(Matrix4 mat, Vector3 sightPos, float fade) {
			Vector3 p = mat * sightPos;
			mat = CreateRotateMatrix(Vector3(0.f, 0.f, 1.f), atan(p.x / p.y) * fade) * mat;
			mat = CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), atan(p.z / p.y) * fade) * mat;
			return mat;
		}

		void Draw2D() {
			if(AimDownSightState > 0.9f) {
			// This here block is for the 2D cross model
				//renderer.ColorP = Vector4(AimDownSightState, AimDownSightState, AimDownSightState, 128.0f); // premultiplied alpha
				
				//float sightSize = 1.f;
				
				// scale sight according to the fov value
				//float fov = cg_fov.FloatValue;
				//fov = tan(fov * 0.5f * 3.141592654f / 180.f);
				//sightSize /= fov;
				//sightSize *= renderer.ScreenHeight;
				
				// scale sight according to the distance from the eye to the sight
				//Matrix4 sightMat = GetViewWeaponMatrix();
				//Vector3 sightPos = sightMat * Vector3(0.f, 0.f, 0.f);
				//float scale = 1.f / sightPos.y;
				//sightSize *= scale;
				
				//renderer.DrawImage(cross,
				//	AABB2(0.f, 0.f, renderer.ScreenWidth, renderer.ScreenHeight));
				return;
			}
			BasicViewWeapon::Draw2D();
		}
		
		
		// redefined from BasicViewWeapon.as
		Matrix4 GetViewWeaponMatrix() {	
			Matrix4 mat;
			// sprinting animation					
			if(sprintState > 0.0) {
				sprintState = quadraticIn(sprintState);
				mat = CreateEulerAnglesMatrix(Vector3(0.2, -0.0, -0.2)*sprintState) * mat;
				mat = CreateTranslateMatrix(Vector3(0.1, -0.1, 0.05)*sprintState) * mat;
			}
			
			// raise gun animation
			if(raiseState < 1.0) {
				float putdown = 1.0 - raiseState;
				putdown = cubicIn(putdown);
				mat = CreateRotateMatrix(Vector3(0.0, 0.0, 1.0),
					putdown * -1.3) * mat;
				mat = CreateRotateMatrix(Vector3(0.0, 1.0, 0.0),
					putdown * 0.2) * mat;
				mat = CreateTranslateMatrix(Vector3(0.1, -0.3, 0.8)
					* putdown) * mat;
			}
			
			// recoil animation
			Vector3 recoilRot;
			Vector3 recoilOffset;
			if(readyState < 0.1) {
				float per = (readyState/0.1);
				per = cubicOut(per);
				recoilRot = Vector3(-0.1, 0.0, 0.0) * per;
				recoilOffset = Vector3(0.0, -0.08, -0.02) * per;
			} else if(readyState < 0.2) {
				recoilRot = Vector3(-0.1, 0.0, 0.0);
				recoilOffset = Vector3(0.0, -0.08, -0.02);
			} else if(readyState < 0.4) {
				float per = ( (readyState-0.2)/(0.4-0.2) );
				per = SmoothStep(per);
				recoilRot = Mix(Vector3(-0.1, 0.0, 0.0), Vector3(0.05, 0.0, 0.0), per);
				recoilOffset = Mix(Vector3(0.0, -0.08, -0.02), Vector3(0.0, 0.0, 0.0), per);
			} else if(readyState < 0.8) {
				float per = ( (readyState-0.4)/(0.8-0.4) );
				per = SmoothStep(per);
				recoilRot = Mix(Vector3(0.05, 0.0, 0.0), Vector3(0.0, 0.0, 0.0), per);
				recoilOffset = Vector3(0.0, 0.0, 0.0);
			}
			// No recoil when the player is aiming. Multiply by (1 - aimScopingState)
			float unSightState = 1.0-AimDownSightStateSmooth;
			mat = CreateEulerAnglesMatrix(recoilRot*unSightState) * mat;
			mat = mat * CreateTranslateMatrix(recoilOffset*unSightState);
			
			// default offset from when the player is not aiming (i.e. default position)
			mat = CreateTranslateMatrix( Mix( Vector3(-0.13, 0.5,0.2), Vector3(0.0, 0.05, -(0.-pivot.z)*globalScale), AimDownSightStateSmooth)) * mat; 

			// offset from when the player is walking
			// again, don't move the gun when the weapon is aimed
			mat = CreateTranslateMatrix(swing * GetMotionGain() * unSightState) * mat;

			// twist the gun when strafing
			// don't rotate when scoped
			mat = mat * CreateEulerAnglesMatrix(Vector3(0.0, 2.0*swing.x, 0.0)*unSightState);
			
			return mat;
		}
		

		
	void AddToScene() {
		
			Matrix4 mat = CreateScaleMatrix(0.033f);
			mat = GetViewWeaponMatrix() * mat;

			bool reloading = IsReloading;
			float reload = ReloadProgress;
			Vector3 leftHand, rightHand;

			leftHand = mat * Vector3(1.f, 8.f, 1.f);
			rightHand = mat * Vector3(.5, -9.0, 2.0);


			if(AimDownSightStateSmooth > 0.8f){
				ModelRenderParam param;
				Matrix4 scopeMatrix = eyeMatrix * CreateScaleMatrix(0.01f) * CreateTranslateMatrix(Vector3(0.0, 50.0, 0.0));
				
				param.matrix = scopeMatrix;
				param.depthHack = true;
				renderer.AddModel(scope, param);
				mat = eyeMatrix;
				leftHand = Vector3(0.f, 0.f, 0.f);
				
			 //this here block is for the voxel model sight	
				param.matrix = scopeMatrix * CreateScaleMatrix(.02f) *
					CreateTranslateMatrix(0.5f, 0.f, 0.5f);
				param.depthHack = true;
				renderer.AddModel(dot, param);
				
				param.matrix = scopeMatrix 
					* CreateTranslateMatrix(0.0f, 30.0f, 12.5f)
					* CreateScaleMatrix(0.1f, 0.1f, 20.0f);
				renderer.AddModel(singleVoxel, param);	
				
				param.matrix = scopeMatrix 
					* CreateTranslateMatrix(0.0f, 30.0f, -12.5f)
					* CreateScaleMatrix(0.1f, 0.1f, 20.0f);
				renderer.AddModel(singleVoxel, param);
				
				param.matrix = scopeMatrix 
					* CreateTranslateMatrix(12.5f, 30.0f, 0.0f)
					* CreateScaleMatrix(20.0f, 0.1f, 0.1f);
				renderer.AddModel(singleVoxel, param);
				
				param.matrix = scopeMatrix 
					* CreateTranslateMatrix(-12.5f, 30.0f, 0.0f)
					* CreateScaleMatrix(20.0f, 0.1f, 0.1f);
				renderer.AddModel(singleVoxel, param);
			}

			ModelRenderParam param; {
			//Shared class weapFrame;
			Matrix4 weapMatrix = eyeMatrix * mat;
			param.matrix = weapMatrix * CreateScaleMatrix(0.2f, .18f, 0.2f) *
				CreateTranslateMatrix(-0.5f, -5.f, -2.f);
			param.depthHack = true;
			Matrix4 cylinderMatrix = eyeMatrix * mat;
			
			//reload sequence, long sequence ahead
				
				if(reloadProgress < 1.) {
					if (reloadProgress < 0.05) { //rotate the gun clockwise a tiny bit to swing it back to eject the cylinder
						float per = ( (reloadProgress-0.0)/(0.05-0.0) );
						per = quadraticIn(per);
						rightHand = mat * (Mix( Vector3(.5, -9.0, 2.0), Vector3(.0, -9.0, 2.0), per));
						leftHand = mat * (Mix( Vector3(1.f, 8.f, 1.f), Vector3(.5, 8.0, 1.0), per));
						mat = mat * CreateEulerAnglesMatrix( Vector3(0., 0.5, 0.) * per ); //this starts normally as you need a baseplate to start from
							
						weapMatrix = eyeMatrix * mat; //this is to add in the models, i got 3 to make the weapon itself
						param.matrix = weapMatrix
						* CreateScaleMatrix(0.2f, .18f, 0.2f) * CreateTranslateMatrix(-0.5f, -5.f, -2.f); //scaling and positioning to the global scale of the weapon when its not reloading
						renderer.AddModel(gunModel, param);
						renderer.AddModel(barrelhandle, param);		
						renderer.AddModel(barrel, param);						
						
							param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the cylinder to match global scale
							*CreateTranslateMatrix(0.5f, -16.0f, -18.f) //standard position of cylinger. preparing to add animations for the cylinder
							* CreateEulerAnglesMatrix( Vector3(0., 0., 0.)* per); //preparing to add animations for the cylinder
							renderer.AddModel(magazineModel, param);
							renderer.AddModel(casing1, param);
							renderer.AddModel(casing2, param);
							renderer.AddModel(casing3, param);
							renderer.AddModel(casing4, param);
							renderer.AddModel(casing5, param);
							renderer.AddModel(casing6, param);
							renderer.AddModel(casing7, param);
							renderer.AddModel(casing8, param);
							renderer.AddModel(casing9, param);
							renderer.AddModel(casing10, param);
			
							
					} 
					
					else if(reloadProgress < 0.1) {
				//for each step we need the time of the previous step and the time of the current step
				//in this case, 0.05 was the previous step and 0.1 is the current step
						float per = ( (reloadProgress-0.05)/(0.1-0.05) );
						per = quadraticIn(per);
						rightHand = mat * (Mix( Vector3(.0, -9.0, 2.0), Vector3(.5, -9.0, 2.0), per));
						leftHand = mat * (Mix( Vector3(0.5, 8.0, 1.0), Vector3(1.0, 8.0, 1.0), per));
						//for every step we are now using mix as it allows the steps to naturally continue eachother
						//This here is standard for all 	this is value of prev step|||This is value of current step
						mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(0., 0.5, 0.) , Vector3(0., -0.2, 0.), per )); 
							
						weapMatrix = eyeMatrix * mat; //this is to add in the models, i got 3 to make the weapon itself
						param.matrix = weapMatrix
						* CreateScaleMatrix(0.2f, .18f, 0.2f) * CreateTranslateMatrix(-0.5f, -5.f, -2.f); //scaling and positioning to the global scale of the weapon when its not reloading
						renderer.AddModel(gunModel, param);
						renderer.AddModel(barrelhandle, param);		
						renderer.AddModel(barrel, param);
						
						param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the cylinder to match global scale
							* CreateTranslateMatrix(Mix (Vector3 (0.5f, -16.0f, -18.f), Vector3 (24.5f, -16.0f, -3.f), per )) //moving the cylinder out of the gun
							* CreateEulerAnglesMatrix(Mix (Vector3(0., 0., 0.), Vector3 (0., -1., 0.), per )); //rotation of the cylinder counter clockwise out of the gun
							renderer.AddModel(magazineModel, param);
							renderer.AddModel(casing1, param);
							renderer.AddModel(casing2, param);
							renderer.AddModel(casing3, param);
							renderer.AddModel(casing4, param);
							renderer.AddModel(casing5, param);
							renderer.AddModel(casing6, param);
							renderer.AddModel(casing7, param);
							renderer.AddModel(casing8, param);
							renderer.AddModel(casing9, param);
							renderer.AddModel(casing10, param);
			


					}
			
					else if(reloadProgress < 0.15) { //this here is a small pause to let the cylinder unroll out of the gun
						float per = ( (reloadProgress-0.1)/(0.15-0.1) );
						per = SmoothStep(per);
						rightHand = mat * (Mix( Vector3(.5, -9.0, 2.0), Vector3(.5, -9.0, 2.0), per));
						leftHand = mat * (Mix( Vector3(1.0, 8.0, 1.0), Vector3(1.0, 8.0, 1.0), per)); 
						mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(0., -0.2, 0.) , Vector3(0., 0., 0.), per )); 
							
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix
						* CreateScaleMatrix(0.2f, .18f, 0.2f) * CreateTranslateMatrix(-0.5f, -5.f, -2.f);
						renderer.AddModel(gunModel, param);
						renderer.AddModel(barrelhandle, param);		
						renderer.AddModel(barrel, param);
						
						param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the cylinder to match global scale
							* CreateTranslateMatrix(Mix (Vector3 (24.5f, -16.0f, -3.f), Vector3 (24.5f, -16.0f, -3.f), per )) //fixed cylinder position relative to the gun
							* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0., -1., 0.), per )); //fixed cylinder position relative to the gun
							renderer.AddModel(magazineModel, param);
							renderer.AddModel(casing1, param);
							renderer.AddModel(casing2, param);
							renderer.AddModel(casing3, param);
							renderer.AddModel(casing4, param);
							renderer.AddModel(casing5, param);
							renderer.AddModel(casing6, param);
							renderer.AddModel(casing7, param);
							renderer.AddModel(casing8, param);
							renderer.AddModel(casing9, param);
							renderer.AddModel(casing10, param);
			

					}
					
					else if(reloadProgress < 0.25) { //lift gun up to get the bullets out of it
						float per = ( (reloadProgress-0.15)/(0.25-0.15) );
						per = quadraticInOut(per);
						rightHand = mat * (Mix( Vector3(.5, -9.0, 2.0), Vector3(-1.2, -2.7, 11.0), per));
						leftHand = mat * (Mix( Vector3(1.0, 8.0, 1.0), Vector3(3.0, 6.0, -3.0), per)); //left hand starts going to cylinder
						mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(0., 0., 0.) , Vector3(-1., 0., 0.), per )); 
						mat = mat * CreateTranslateMatrix(( Vector3(.0, 5.0, .0)* per)); 
							
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix
						* CreateScaleMatrix(0.2f, .18f, 0.2f) * CreateTranslateMatrix(-0.5f, -5.f, -2.f);
						renderer.AddModel(gunModel, param);
						renderer.AddModel(barrelhandle, param);		
						renderer.AddModel(barrel, param);
						
						
						param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the cylinder to match global scale
							* CreateTranslateMatrix(Mix (Vector3 (24.5f, -16.0f, -3.f), Vector3 (24.5f, -16.0f, -3.f), per )) //fixed cylinder position relative to the gun
							* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0., -1., 0.), per )); //fixed cylinder position relative to the gun
							renderer.AddModel(magazineModel, param);
							
							//casings are still in the gun. no rotation needed yet
							per = quadraticIn(per);
								param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
								* CreateTranslateMatrix(Mix (Vector3 (24.5f, -16.0f, -3.f), Vector3 (24.5f, -38.0f, -3.f), per )) //position of the casings will change during the next few steps
								* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0., -1., 0.), per )); //rotation of the casings will change during the next few steps
								renderer.AddModel(casing1, param);
							
									param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
									* CreateTranslateMatrix(Mix (Vector3 (24.5f, -16.0f, -3.f), Vector3 (24.5f, -41.0f, -3.f), per )) //position of the casings will change during the next few steps
									* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0., -1., 0.), per )); //rotation of the casings will change during the next few steps
									renderer.AddModel(casing2, param);
							
										param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
										* CreateTranslateMatrix(Mix (Vector3 (24.5f, -16.0f, -3.f), Vector3 (24.5f, -48.0f, -3.f), per )) //position of the casings will change during the next few steps
										* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0., -1., 0.), per )); //rotation of the casings will change during the next few steps
										renderer.AddModel(casing3, param);
							
											param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
											* CreateTranslateMatrix(Mix (Vector3 (24.5f, -16.0f, -3.f), Vector3 (24.5f, -39.0f, -3.f), per )) //position of the casings will change during the next few steps
											* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0., -1., 0.), per )); //rotation of the casings will change during the next few steps
											renderer.AddModel(casing4, param);
							
												param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
												* CreateTranslateMatrix(Mix (Vector3 (24.5f, -16.0f, -3.f), Vector3 (24.5f, -40.0f, -3.f), per )) //position of the casings will change during the next few steps
												* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0., -1., 0.), per )); //rotation of the casings will change during the next few steps
												renderer.AddModel(casing5, param);
							
													param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
													* CreateTranslateMatrix(Mix (Vector3 (24.5f, -16.0f, -3.f), Vector3 (24.5f, -46.0f, -3.f), per )) //position of the casings will change during the next few steps
													* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0., -1., 0.), per )); //rotation of the casings will change during the next few steps
													renderer.AddModel(casing6, param);
							
														param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
														* CreateTranslateMatrix(Mix (Vector3 (24.5f, -16.0f, -3.f), Vector3 (24.5f, -45.0f, -3.f), per )) //position of the casings will change during the next few steps
														* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0., -1., 0.), per )); //rotation of the casings will change during the next few steps
														renderer.AddModel(casing7, param);
							
															param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
															* CreateTranslateMatrix(Mix (Vector3 (24.5f, -16.0f, -3.f), Vector3 (24.5f, -41.0f, -3.f), per )) //position of the casings will change during the next few steps
															* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0., -1., 0.), per )); //rotation of the casings will change during the next few steps
															renderer.AddModel(casing8, param);
							
																param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
																* CreateTranslateMatrix(Mix (Vector3 (24.5f, -16.0f, -3.f), Vector3 (24.5f, -42.0f, -3.f), per )) //position of the casings will change during the next few steps
																* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0., -1., 0.), per )); //rotation of the casings will change during the next few steps
																renderer.AddModel(casing9, param);
							
																	param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
																	* CreateTranslateMatrix(Mix (Vector3 (24.5f, -16.0f, -3.f), Vector3 (24.5f, -39.0f, -3.f), per )) //position of the casings will change during the next few steps
																	* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0., -1., 0.), per )); //rotation of the casings will change during the next few steps
																	renderer.AddModel(casing10, param);
			
	
					}
					else if(reloadProgress < 0.3) { //jostle the gun up and down to help the bullets fall down
						float per = ( (reloadProgress-0.25)/(0.3-0.25) );
						per = quadraticOut(per);
						rightHand = mat * (Mix( Vector3(-1.2, -2.7, 11.0), Vector3(-1.2, -2.7, 13.0), per));
						leftHand = mat * (Mix( Vector3(3.0, 6.0, -3.0), Vector3(3.0, 6.0, -1.0), per));
						mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-1., 0., 0.) , Vector3(-1., 0., 0.), per )); //angle fixed viewed from the character
						mat = mat * CreateTranslateMatrix(Mix ( Vector3(.0, 5.0, .0), Vector3 (.0, 4.0, .0), per)) ; // push the gun up
							
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix
						* CreateScaleMatrix(0.2f, .18f, 0.2f) * CreateTranslateMatrix(-0.5f, -5.f, -2.f);
						renderer.AddModel(gunModel, param);
						renderer.AddModel(barrelhandle, param);		
						renderer.AddModel(barrel, param);
						
						param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the cylinder to match global scale
							* CreateTranslateMatrix(Mix (Vector3 (24.5f, -16.0f, -3.f), Vector3 (24.5f, -16.0f, -3.f), per )) //fixed cylinder position relative to the gun
							* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0., -1., 0.), per )); //fixed cylinder position relative to the gun
							renderer.AddModel(magazineModel, param);
							
							//here is where the fun starts..........<
							per = SmoothStep(per);
								param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
								* CreateTranslateMatrix(Mix (Vector3 (24.5f, -38.0f, -3.f), Vector3 (24.5f, -65.0f, -3.f), per )) //position of the casings will change during the next few steps
								* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0.1, -1., 0.), per )); //rotation of the casings will change during the next few steps
								renderer.AddModel(casing1, param);
							
									param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
									* CreateTranslateMatrix(Mix (Vector3 (24.5f, -41.0f, -3.f), Vector3 (24.5f, -68.0f, -3.f), per )) //position of the casings will change during the next few steps
									* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0.2, -1., 0.), per )); //rotation of the casings will change during the next few steps
									renderer.AddModel(casing2, param);
							
										param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
										* CreateTranslateMatrix(Mix (Vector3 (24.5f, -48.0f, -3.f), Vector3 (24.5f, -79.0f, -3.f), per )) //position of the casings will change during the next few steps
										* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0.2, -1., 0.), per )); //rotation of the casings will change during the next few steps
										renderer.AddModel(casing3, param);
							
											param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
											* CreateTranslateMatrix(Mix (Vector3 (24.5f, -39.0f, -3.f), Vector3 (24.5f, -66.0f, -3.f), per )) //position of the casings will change during the next few steps
											* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0.1, -1., 0.), per )); //rotation of the casings will change during the next few steps
											renderer.AddModel(casing4, param);
							
												param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
												* CreateTranslateMatrix(Mix (Vector3 (24.5f, -40.0f, -3.f), Vector3 (24.5f, -68.0f, -3.f), per )) //position of the casings will change during the next few steps
												* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0.2, -1., 0.), per )); //rotation of the casings will change during the next few steps
												renderer.AddModel(casing5, param);
							
													param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
													* CreateTranslateMatrix(Mix (Vector3 (24.5f, -46.0f, -3.f), Vector3 (24.5f, -80.0f, -3.f), per )) //position of the casings will change during the next few steps
													* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0.1, -1., 0.), per )); //rotation of the casings will change during the next few steps
													renderer.AddModel(casing6, param);
							
														param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
														* CreateTranslateMatrix(Mix (Vector3 (24.5f, -45.0f, -3.f), Vector3 (24.5f, -79.0f, -3.f), per )) //position of the casings will change during the next few steps
														* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0.1, -1., 0.2), per )); //rotation of the casings will change during the next few steps
														renderer.AddModel(casing7, param);
							
															param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
															* CreateTranslateMatrix(Mix (Vector3 (24.5f, -41.0f, -3.f), Vector3 (24.5f, -70.0f, -3.f), per )) //position of the casings will change during the next few steps
															* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0.2, -1., 0.), per )); //rotation of the casings will change during the next few steps
															renderer.AddModel(casing8, param);
							
																param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
																* CreateTranslateMatrix(Mix (Vector3 (24.5f, -42.0f, -3.f), Vector3 (24.5f, -72.0f, -3.f), per )) //position of the casings will change during the next few steps
																* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0.1, -1., 0.), per )); //rotation of the casings will change during the next few steps
																renderer.AddModel(casing9, param);
							
																	param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
																	* CreateTranslateMatrix(Mix (Vector3 (24.5f, -39.0f, -3.f), Vector3 (24.5f, -66.0f, -3.f), per )) //position of the casings will change during the next few steps
																	* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0.2, -1., 0.), per )); //rotation of the casings will change during the next few steps
																	renderer.AddModel(casing10, param);
			


					}
					else if(reloadProgress < 0.35) { //jostle the gun up and down to help the bullets fall down
						float per = ( (reloadProgress-0.3)/(0.35-0.3) );
						per = quadraticIn(per);
						rightHand = mat * (Mix( Vector3(-1.2, -2.7, 13.0), Vector3(-1.2, -2.7, 11.0), per));
						leftHand = mat * (Mix( Vector3(3.0, 6.0, -1.0), Vector3(1.0, 5.0, 10.0), per)); //left hand goes to get a speedloader
						mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-1., 0., 0.) , Vector3(-1., 0., 0.), per )); //angle fixed viewed from the character
						mat = mat * CreateTranslateMatrix(Mix ( Vector3(.0, 5.0, .0), Vector3 (.0, 6.0, .0), per)) ; // push the gun up
							
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix
						* CreateScaleMatrix(0.2f, .18f, 0.2f) * CreateTranslateMatrix(-0.5f, -5.f, -2.f);
						renderer.AddModel(gunModel, param);
						renderer.AddModel(barrelhandle, param);		
						renderer.AddModel(barrel, param);
						
						param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the cylinder to match global scale
							* CreateTranslateMatrix(Mix (Vector3 (24.5f, -16.0f, -3.f), Vector3 (24.5f, -16.0f, -3.f), per )) //fixed cylinder position relative to the gun
							* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0., -1., 0.), per )); //fixed cylinder position relative to the gun
							renderer.AddModel(magazineModel, param);
							
							per = SmoothStep(per);
								param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
								* CreateTranslateMatrix(Mix (Vector3 (24.5f, -65.0f, -3.f), Vector3 (24.5f, -115.0f, -3.f), per )) //position of the casings will change during the next few steps
								* CreateEulerAnglesMatrix(Mix (Vector3(0.1, -1., 0.), Vector3 (0.5, -1.5, 0.), per )); //rotation of the casings will change during the next few steps
								renderer.AddModel(casing1, param);
							
									param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
									* CreateTranslateMatrix(Mix (Vector3 (24.5f, -68.0f, -3.f), Vector3 (24.5f, -120.0f, -3.f), per )) //position of the casings will change during the next few steps
									* CreateEulerAnglesMatrix(Mix (Vector3(0.2, -1., 0.), Vector3 (0.7, -1.5, 0.), per )); //rotation of the casings will change during the next few steps
									renderer.AddModel(casing2, param);
							
										param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
										* CreateTranslateMatrix(Mix (Vector3 (24.5f, -79.0f, -3.f), Vector3 (24.5f, -140.0f, -3.f), per )) //position of the casings will change during the next few steps
										* CreateEulerAnglesMatrix(Mix (Vector3(0.2, -1., 0.), Vector3 (0.5, -1.5, 0.), per )); //rotation of the casings will change during the next few steps
										renderer.AddModel(casing3, param);
							
											param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
											* CreateTranslateMatrix(Mix (Vector3 (24.5f, -66.0f, -3.f), Vector3 (24.5f, -117.0f, -3.f), per )) //position of the casings will change during the next few steps
											* CreateEulerAnglesMatrix(Mix (Vector3(0.1, -1., 0.), Vector3 (0.3, -1.5, 0.), per )); //rotation of the casings will change during the next few steps
											renderer.AddModel(casing4, param);
							
												param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
												* CreateTranslateMatrix(Mix (Vector3 (24.5f, -68.0f, -3.f), Vector3 (24.5f, -122.0f, -3.f), per )) //position of the casings will change during the next few steps
												* CreateEulerAnglesMatrix(Mix (Vector3(0.2, -1., 0.), Vector3 (0.6, -1.5, 0.), per )); //rotation of the casings will change during the next few steps
												renderer.AddModel(casing5, param);
							
													param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
													* CreateTranslateMatrix(Mix (Vector3 (24.5f, -80.0f, -3.f), Vector3 (24.5f, -150.0f, -3.f), per )) //position of the casings will change during the next few steps
													* CreateEulerAnglesMatrix(Mix (Vector3(0.1, -1., 0.), Vector3 (0.4, -1.5, 0.), per )); //rotation of the casings will change during the next few steps
													renderer.AddModel(casing6, param);
							
														param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
														* CreateTranslateMatrix(Mix (Vector3 (24.5f, -79.0f, -3.f), Vector3 (24.5f, -148.0f, -3.f), per )) //position of the casings will change during the next few steps
														* CreateEulerAnglesMatrix(Mix (Vector3(0.1, -1., 0.), Vector3 (0.3, -1.5, 0.), per )); //rotation of the casings will change during the next few steps
														renderer.AddModel(casing7, param);
							
															param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
															* CreateTranslateMatrix(Mix (Vector3 (24.5f, -70.0f, -3.f), Vector3 (24.5f, -130.0f, -3.f), per )) //position of the casings will change during the next few steps
															* CreateEulerAnglesMatrix(Mix (Vector3(0.2, -1., 0.), Vector3 (0.5, -1.5, 0.), per )); //rotation of the casings will change during the next few steps
															renderer.AddModel(casing8, param);
							
																param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
																* CreateTranslateMatrix(Mix (Vector3 (24.5f, -72.0f, -3.f), Vector3 (24.5f, -130.0f, -3.f), per )) //position of the casings will change during the next few steps
																* CreateEulerAnglesMatrix(Mix (Vector3(0.1, -1., 0.), Vector3 (0.4, -1.5, 0.), per )); //rotation of the casings will change during the next few steps
																renderer.AddModel(casing9, param);
							
																	param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
																	* CreateTranslateMatrix(Mix (Vector3 (24.5f, -66.0f, -3.f), Vector3 (24.5f, -120.0f, -3.f), per )) //position of the casings will change during the next few steps
																	* CreateEulerAnglesMatrix(Mix (Vector3(0.2, -1., 0.0), Vector3 (0.6, -1.5, 0.), per )); //rotation of the casings will change during the next few steps
																	renderer.AddModel(casing10, param);  

					}
					else if(reloadProgress < 0.40) { //jostle the gun up and down to help the bullets fall down
						float per = ( (reloadProgress-0.35)/(0.40-0.35) );
						per = quadraticOut(per);
						rightHand = mat * (Mix( Vector3(-1.2, -2.7, 11.0), Vector3(-1.2, -2.7, 13.0), per));
						leftHand = mat * (Mix( Vector3(1.0, 5.0, 10.0), Vector3(1.0, 5.0, 10.0), per));
						mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-1., 0., 0.) , Vector3(-1., 0., 0.), per )); //angle fixed viewed from the character
						mat = mat * CreateTranslateMatrix(Mix ( Vector3(.0, 5.0, .0), Vector3 (.0, 4.0, .0), per)) ; // push the gun up
							
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix
						* CreateScaleMatrix(0.2f, .18f, 0.2f) * CreateTranslateMatrix(-0.5f, -5.f, -2.f);
						renderer.AddModel(gunModel, param);
						renderer.AddModel(barrelhandle, param);		
						renderer.AddModel(barrel, param);
						
						param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the cylinder to match global scale
							* CreateTranslateMatrix(Mix (Vector3 (24.5f, -16.0f, -3.f), Vector3 (24.5f, -16.0f, -3.f), per )) //fixed cylinder position relative to the gun
							* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0., -1., 0.), per )); //fixed cylinder position relative to the gun
							renderer.AddModel(magazineModel, param);
							
							per = SmoothStep(per);
								param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
								* CreateTranslateMatrix(Mix (Vector3 (24.5f, -115.0f, -3.f), Vector3 (24.5f, -250.0f, -3.f), per )) //position of the casings will change during the next few steps
								* CreateEulerAnglesMatrix(Mix (Vector3(0.1, -1.5, 0.5), Vector3 (1., -2., 0.), per )); //rotation of the casings will change during the next few steps
								renderer.AddModel(casing1, param);
							
									param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
									* CreateTranslateMatrix(Mix (Vector3 (24.5f, -120.0f, -3.f), Vector3 (24.5f, -260.0f, -3.f), per )) //position of the casings will change during the next few steps
									* CreateEulerAnglesMatrix(Mix (Vector3(0.2, -1.5, 0.3), Vector3 (1., -2., 0.), per )); //rotation of the casings will change during the next few steps
									renderer.AddModel(casing2, param);
							
										param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
										* CreateTranslateMatrix(Mix (Vector3 (24.5f, -140.0f, -3.f), Vector3 (24.5f, -300.0f, -3.f), per )) //position of the casings will change during the next few steps
										* CreateEulerAnglesMatrix(Mix (Vector3(0.2, -1.5, 0.6), Vector3 (1., -2., 0.), per )); //rotation of the casings will change during the next few steps
										renderer.AddModel(casing3, param);
							
											param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
											* CreateTranslateMatrix(Mix (Vector3 (24.5f, -117.0f, -3.f), Vector3 (24.5f, -250.0f, -3.f), per )) //position of the casings will change during the next few steps
											* CreateEulerAnglesMatrix(Mix (Vector3(0.1, -1.5, 0.4), Vector3 (0., -2., 0.), per )); //rotation of the casings will change during the next few steps
											renderer.AddModel(casing4, param);
							
												param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
												* CreateTranslateMatrix(Mix (Vector3 (24.5f, -122.0f, -3.f), Vector3 (24.5f, -260.0f, -3.f), per )) //position of the casings will change during the next few steps
												* CreateEulerAnglesMatrix(Mix (Vector3(0.2, -1.5, 0.7), Vector3 (0., -2., 0.), per )); //rotation of the casings will change during the next few steps
												renderer.AddModel(casing5, param);
							
													param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
													* CreateTranslateMatrix(Mix (Vector3 (24.5f, -150.0f, -3.f), Vector3 (24.5f, -320.0f, -3.f), per )) //position of the casings will change during the next few steps
													* CreateEulerAnglesMatrix(Mix (Vector3(0.1, -1.5, 0.4), Vector3 (0., -2., 0.), per )); //rotation of the casings will change during the next few steps
													renderer.AddModel(casing6, param);
							
														param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
														* CreateTranslateMatrix(Mix (Vector3 (24.5f, -148.0f, -3.f), Vector3 (24.5f, -320.0f, -3.f), per )) //position of the casings will change during the next few steps
														* CreateEulerAnglesMatrix(Mix (Vector3(0.1, -1.5, 0.6), Vector3 (0., -2., 0.), per )); //rotation of the casings will change during the next few steps
														renderer.AddModel(casing7, param);
							
															param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
															* CreateTranslateMatrix(Mix (Vector3 (24.5f, -130.0f, -3.f), Vector3 (24.5f, -290.0f, -3.f), per )) //position of the casings will change during the next few steps
															* CreateEulerAnglesMatrix(Mix (Vector3(0.2, -1.5, 0.8), Vector3 (0., -2., 0.), per )); //rotation of the casings will change during the next few steps
															renderer.AddModel(casing8, param);
							
																param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
																* CreateTranslateMatrix(Mix (Vector3 (24.5f, -130.0f, -3.f), Vector3 (24.5f, -290.0f, -3.f), per )) //position of the casings will change during the next few steps
																* CreateEulerAnglesMatrix(Mix (Vector3(0.1, -1.5, 0.6), Vector3 (0., -2., 0.), per )); //rotation of the casings will change during the next few steps
																renderer.AddModel(casing9, param);
							
																	param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the casings top match the cylinder
																	* CreateTranslateMatrix(Mix (Vector3 (24.5f, -120.0f, -3.f), Vector3 (24.5f, -260.0f, -3.f), per )) //position of the casings will change during the next few steps
																	* CreateEulerAnglesMatrix(Mix (Vector3(0.2, -1.5, 0.), Vector3 (0., -2., 0.), per )); //rotation of the casings will change during the next few steps
																	renderer.AddModel(casing10, param);   

					}
					else if(reloadProgress < 0.5) { //reset the gun's postion to default but slightly aimed downward to facilitate putting bullets in
						float per = ( (reloadProgress-0.4)/(0.5-0.4) );
						per = quadraticInOut(per);
						rightHand = mat * (Mix( Vector3(-1.2, -2.7, 13.0), Vector3(.5, -9.0, 2.0), per) );
						leftHand = mat * (Mix( Vector3(1.0, 5.0, 10.0), Vector3(3.0, -3.0, 0.0), per)); //left hand comes back with speedloader
						mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(-1., 0., 0.) , Vector3(0.2, 0., 0.), per )); //angles gun slightly down
						mat = mat * CreateTranslateMatrix(Mix ( Vector3(.0, 4.0, .0), Vector3 (.0, .0, .0), per)) ; //reset position of the gun to default
							
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix
						* CreateScaleMatrix(0.2f, .18f, 0.2f) * CreateTranslateMatrix(-0.5f, -5.f, -2.f);
						renderer.AddModel(gunModel, param);
						renderer.AddModel(barrelhandle, param);		
						renderer.AddModel(barrel, param);
						
						param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the cylinder to match global scale
							* CreateTranslateMatrix(Mix (Vector3 (24.5f, -16.0f, -3.f), Vector3 (24.5f, -16.0f, -3.f), per )) //fixed cylinder position relative to the gun
							* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0., -1., 0.), per )); //fixed cylinder position relative to the gun
							renderer.AddModel(magazineModel, param);

					}
					else if(reloadProgress < 0.55) { //create a pause for the character to put the bullets in
						float per = ( (reloadProgress-0.5)/(0.55-0.5) );
						per = SmoothStep(per);
						rightHand = mat * (Mix( Vector3(.5, -9.0, 2.0), Vector3(.5, -9.0, 2.0), per) );
						leftHand = mat * (Mix( Vector3(3.0, -3.0, 0.0), Vector3(3.0, -3.0, 0.0), per)); //left hand loads speedloader in
						mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(.2, 0., 0.) , Vector3(0.2, 0., 0.), per )); //fixed position
						mat = mat * CreateTranslateMatrix(Mix ( Vector3(.0, .0, .0), Vector3 (.0, .0, .0), per)) ; //fixed default position
							
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix
						* CreateScaleMatrix(0.2f, .18f, 0.2f) * CreateTranslateMatrix(-0.5f, -5.f, -2.f);
						renderer.AddModel(gunModel, param);
						renderer.AddModel(barrelhandle, param);		
						renderer.AddModel(barrel, param);
						
						param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the cylinder to match global scale
							* CreateTranslateMatrix(Mix (Vector3 (24.5f, -16.0f, -3.f), Vector3 (24.5f, -16.0f, -3.f), per )) //fixed cylinder position relative to the gun
							* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0., -1., 0.), per )); //fixed cylinder position relative to the gun
							renderer.AddModel(magazineModel, param);
							renderer.AddModel(bulletcyl, param);

					}
					else if(reloadProgress < 0.58) { //close the cylinder 
						float per = ( (reloadProgress-0.55)/(0.58-0.55) );
						per = quadraticIn(per);
						rightHand = mat * (Mix( Vector3(.5, -9.0, 2.0), Vector3(.5, -9.0, 2.0), per));
						leftHand = mat * (Mix( Vector3(3.0, -3.0, 0.0), Vector3(5.0, -3.0, 0.0), per));
						mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(.2, 0., 0.) , Vector3(0., 0., 0.), per )); //fixed position
						mat = mat * CreateTranslateMatrix(Mix ( Vector3(.0, .0, .0), Vector3 (.0, .0, .0), per)) ; 
							
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix
						* CreateScaleMatrix(0.2f, .18f, 0.2f) * CreateTranslateMatrix(-0.5f, -5.f, -2.f);
						renderer.AddModel(gunModel, param);
						renderer.AddModel(barrelhandle, param);		
						renderer.AddModel(barrel, param);
						
						param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the cylinder to match global scale
							* CreateTranslateMatrix(Mix (Vector3 (24.5f, -16.0f, -3.f), Vector3 (0.5f, -16.0f, -18.f), per )) //closing the cylinder
							* CreateEulerAnglesMatrix(Mix (Vector3(0., -1., 0.), Vector3 (0., 0., 0.), per )); //closing the cylinder
							renderer.AddModel(magazineModel, param);
							renderer.AddModel(bulletcyl, param);

					}
					else if(reloadProgress < 0.61) { //close the cylinder 
						float per = ( (reloadProgress-0.58)/(0.61-0.58) );
						per = SmoothStep(per);
						rightHand = mat * (Mix( Vector3(.5, -9.0, 2.0), Vector3(.5, -9.0, 2.0), per));
						leftHand = mat * (Mix( Vector3(5.0, -3.0, 0.0), Vector3(7.0, -3.0, -5.0), per));
						mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(.0, 0., 0.) , Vector3(0., 0., 0.), per )); //fixed position
						mat = mat * CreateTranslateMatrix(Mix ( Vector3(.0, .0, .0), Vector3 (.0, .0, .0), per)) ; 
							
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix
						* CreateScaleMatrix(0.2f, .18f, 0.2f) * CreateTranslateMatrix(-0.5f, -5.f, -2.f);
						renderer.AddModel(gunModel, param);
						renderer.AddModel(barrelhandle, param);		
						renderer.AddModel(barrel, param);
						
						param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the cylinder to match global scale
							* CreateTranslateMatrix(Mix (Vector3 (0.5f, -16.0f, -18.f), Vector3 (0.5f, -16.0f, -18.f), per )) 
							* CreateEulerAnglesMatrix(Mix (Vector3(0., 0., 0.), Vector3 (0., 1., 0.), per )); //start spin
							renderer.AddModel(magazineModel, param);
							renderer.AddModel(bulletcyl, param);
					}
					
					
					else if(reloadProgress < 1.) { //close the cylinder 
						float per = ( (reloadProgress-0.61)/(1.-0.61) );
						per = SmoothStep(per);
						rightHand = mat * (Mix( Vector3(.5, -9.0, 2.0), Vector3(.5, -9.0, 2.0), per));
						leftHand = mat * (Mix( Vector3(7.0, -3.0, -5.0), Vector3(1.f, 8.f, 1.f), per));
						mat = mat * CreateEulerAnglesMatrix( Mix (Vector3(.0, 0., 0.) , Vector3(0., 0., 0.), per )); 
						mat = mat * CreateTranslateMatrix(Mix ( Vector3(.0, .0, .0), Vector3 (.0, .0, .0), per)) ; 
							
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix
						* CreateScaleMatrix(0.2f, .18f, 0.2f) * CreateTranslateMatrix(-0.5f, -5.f, -2.f);
						renderer.AddModel(gunModel, param);
						renderer.AddModel(barrelhandle, param);		
						renderer.AddModel(barrel, param);
						
						param.matrix = weapMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f) //scaling of the cylinder to match global scale
							* CreateTranslateMatrix(Mix (Vector3 (0.5f, -16.0f, -18.f), Vector3 (0.5f, -16.0f, -18.f), per )) 
							* CreateEulerAnglesMatrix(Mix (Vector3(0., 15., 0.), Vector3 (0., 30., 0.), per )); //SPIIIIIIIIN
							renderer.AddModel(magazineModel, param);
							renderer.AddModel(bulletcyl, param);

					}
				}
			
			
			else {
			(!reloading);
			renderer.AddModel(gunModel, param);
			renderer.AddModel(barrelhandle, param);		
			renderer.AddModel(barrel, param);		
			}
			

			
			// HAMMER ANIMATION			
			
			weapMatrix = eyeMatrix * mat;
			param.matrix = weapMatrix * CreateScaleMatrix(0.10f) *
				CreateTranslateMatrix(-6.5f, -55.f,  -25.5f);
			param.depthHack = true;
            
            if (readyState > 0.0f && readyState < 0.05f) {
                param.matrix = weapMatrix * CreateScaleMatrix(0.10f) *
                    CreateTranslateMatrix(-6.5f, -55.f,  -25.5f)*
                    CreateRotateMatrix(Vector3(0.5f, 0.f, 0.f), 0.05f/readyState);
            }
            else if (readyState > 0.f && readyState < 0.20f) {
                param.matrix = weapMatrix * CreateScaleMatrix(0.10f) *
                    CreateTranslateMatrix(-6.5f, -55.f, -25.5f)*
                    CreateRotateMatrix(Vector3(-2.f, 0.f, 0.f), 0.74f)*
                    CreateRotateMatrix(Vector3(-30.f, 0.f, 0.f), -0.125f/readyState);
            }
            else {
                param.matrix = weapMatrix * CreateScaleMatrix(0.10f) *
                    CreateTranslateMatrix(-6.5f, -55.f, -25.5f)*
                    CreateRotateMatrix(Vector3(-2.f, 0.f, 0.f), 0.74f);
            }
			
			if (reloading) {
				renderer.AddModel(hammer, param);
			}
			
			else if (!reloading) {		
					param.depthHack = true;
					renderer.AddModel(hammer, param);	
			}
			
			/*param.matrix = cylinderMatrix * CreateTranslateMatrix(15.f, 60.f, 0.f);
			renderer.AddModel(magazineModel, param);
			renderer.AddModel(casing1, param);
			renderer.AddModel(casing2, param);
			renderer.AddModel(casing3, param);
			renderer.AddModel(casing4, param);
			renderer.AddModel(casing5, param);
			renderer.AddModel(casing6, param);
			renderer.AddModel(casing7, param);
			renderer.AddModel(casing8, param);
			renderer.AddModel(casing9, param);
			renderer.AddModel(casing10, param);*/


			// rendering the cylinder as normal

			param.matrix = cylinderMatrix * CreateScaleMatrix(0.115f, 0.08f, 0.115f)*
				CreateTranslateMatrix(0.5f, 0.f, 0.f);
			param.depthHack = true;
			
			if (!reloading) {
				if (readyState > 0.3f && readyState < 0.85f) param.matrix = param.matrix * CreateTranslateMatrix(0.5f, -16.0f, -18.f) *
					CreateRotateMatrix(Vector3(0.f, -2.f, 0.f), 0.585f/readyState);
                
				else param.matrix = param.matrix * CreateTranslateMatrix(0.5f, -16.0f, -18.f);
				
				renderer.AddModel(magazineModel, param);
			}
			
			if (!reloading) {
				renderer.AddModel(bulletcyl, param);
			}
	
			LeftHandPosition = leftHand;
			RightHandPosition = rightHand;
		}
	}
	}

	IWeaponSkin@ CreateViewRifleSkin(Renderer@ r, AudioDevice@ dev) {
		return ViewRifleSkin(r, dev);
	}
}