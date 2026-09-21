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
		private Model@ scopemodel;
		private Model@ boltmodel;

		private AudioChunk@ fireSound;
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@ fireSmallReverbSound;
		private AudioChunk@ fireLargeReverbSound;
		private AudioChunk@ reloadSound;
		private Model@ sight;

		ViewRifleSkin(Renderer@ r, AudioDevice@ dev){
			super(r);
			@audioDevice = dev;
			@gunModel = renderer.RegisterModel
				("Models/Weapons/Rifle/1.kv6");
			@gunModel2= renderer.RegisterModel
				("Models/Weapons/Rifle/2.kv6");
			@magazineModel = renderer.RegisterModel
				("Models/Weapons/Rifle/Magazine.kv6");
			@sight= renderer.RegisterModel
				("Models/Weapons/Rifle/Sight.kv6");
			@scopemodel= renderer.RegisterModel
				("Models/Weapons/Rifle/ScopeModel.kv6");
			@boltmodel= renderer.RegisterModel
				("Models/Weapons/Rifle/BoltHandle.kv6");

			@fireSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/FireLocal.wav");
			@fireFarSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/FireFar.wav");
			@fireStereoSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/FireLocal.wav");
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
				param.volume = 8.f;
				audioDevice.PlayLocal(fireSound, origin, param);

				param.volume = 8.f * environmentRoom;
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
				param.volume = 0.2f;
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
			if(AimDownSightState > 0.6){
			if(png.FloatValue > 0.99){
				Image@ img = renderer.RegisterImage("Gfx/scope1.png");
				float height = renderer.ScreenHeight;
				float width = height * (800.f / 600.f); 
				renderer.Color = (Vector4(1.f, 1.f, 1.f, 1.f));
				renderer.DrawImage(img,
					AABB2((renderer.ScreenWidth - width) * 0.5f,
							(renderer.ScreenHeight - height) * 0.5f,
							width, height));
				return;
				}
				
			if(png2.FloatValue > 0.99){
				Image@ img = renderer.RegisterImage("Gfx/scope2.png");
				float height = renderer.ScreenHeight;
				float width = height * (800.f / 600.f); 
				renderer.Color = (Vector4(1.f, 1.f, 1.f, 1.f));
				renderer.DrawImage(img,
					AABB2((renderer.ScreenWidth - width) * 0.5f,
							(renderer.ScreenHeight - height) * 0.5f,
							width, height));
				return;
				}
				
			if(png3.FloatValue > 0.99){
				Image@ img = renderer.RegisterImage("Gfx/scope3.png");
				float height = renderer.ScreenHeight;
				float width = height * (800.f / 600.f); 
				renderer.Color = (Vector4(1.f, 1.f, 1.f, 1.f));
				renderer.DrawImage(img,
					AABB2((renderer.ScreenWidth - width) * 0.5f,
							(renderer.ScreenHeight - height) * 0.5f,
							width, height));
				return;
				}
		      
		    }
			BasicViewWeapon::Draw2D();
		}

		void AddToScene() {
		    if(AimDownSightStateSmooth > 0.8){
			if(png.FloatValue > 0.99){
				LeftHandPosition = Vector3(1.f, 6.f, 10.f);
				RightHandPosition = Vector3(0.f, -8.f, 20.f);
				return;
				}
			if(png2.FloatValue > 0.99){
				LeftHandPosition = Vector3(1.f, 6.f, 10.f);
				RightHandPosition = Vector3(0.f, -8.f, 20.f);
				return;
				}
			if(png3.FloatValue > 0.99){
				LeftHandPosition = Vector3(1.f, 6.f, 10.f);
				RightHandPosition = Vector3(0.f, -8.f, 20.f);
				return;
				}
			}
			
			Matrix4 mat = CreateScaleMatrix(0.033f);
			mat = GetViewWeaponMatrix() * mat;
			// Apply sight alignment before solving the hand and reload anchors.
			if(AimDownSightStateSmooth > 0.8f){
				mat = AdjustToAlignSight(mat, Vector3(-0.01f, 16.f, -3.80f), (AimDownSightStateSmooth - 0.8f) / 0.2f);
			}

			bool reloading = IsReloading;
			float reload = ReloadProgress;
			Vector3 leftHand, rightHand;

			leftHand = mat * Vector3(1.f, 6.f, 1.f);
			rightHand = mat * Vector3(0.f, -11.f, 2.f);

			Vector3 bolt = mat * Vector3(-2.f, -5.f, -3.f);
			Vector3 boltBack = mat * Vector3(-2.f, -13.f, -3.f);
			Vector3 handUp = mat * Vector3(-2.f, -2.f, -10.f);
			Vector3 insert = mat * Vector3(-1.f, -1.f, -7.f);
	
	
			ModelRenderParam param;
			Matrix4 weapMatrix = eyeMatrix * mat;
			param.matrix = weapMatrix * CreateScaleMatrix(0.14f) *
				CreateTranslateMatrix(-0.5f, 20.f, 0.f);
			param.depthHack = true;
			renderer.AddModel(gunModel, param);
			
			param.matrix = weapMatrix * CreateScaleMatrix(0.14f) *
				CreateTranslateMatrix(-0.5f, 20.f, 0.f);
			param.depthHack = true;
			renderer.AddModel(gunModel2, param);

			// draw sights
			Matrix4 sightMat = weapMatrix;
			sightMat = weapMatrix;
			sightMat *= CreateTranslateMatrix(0.060f, 28.f, -2.6f);
			sightMat *= CreateScaleMatrix(.075f);
			param.matrix = sightMat;
			renderer.AddModel(sight, param); // front pin
			
			
			if(scope.FloatValue > 0.99){
			sightMat *= CreateTranslateMatrix(0.f, -290.f, -12.f);;
			sightMat *= CreateScaleMatrix(.9f);
			param.matrix = sightMat;
			renderer.AddModel(scopemodel, param); // rear
			}

			
			mat *= CreateTranslateMatrix(0.f, 1.f, 1.f);
			reload *= 2.5f;
			
			// hands
			if(reloading){
				if(reload < 0.1f){
				
					float per = reload / 0.1f;
					rightHand = Mix(rightHand, bolt, SmoothStep(per));
				}
				else if(reload < 0.5f){
				
					float per = (reload - 0.2) / 0.4f;
					rightHand = Mix(bolt, boltBack, SmoothStep(per));
				}
				else if(reload < .9f){
				
					float per = (reload - 0.5f) / 0.38f;
					rightHand = Mix(boltBack, handUp, SmoothStep(per));
				}
				else if(reload < 1.0f){
				
					float per = (reload - 0.9f) / 0.2f;
					rightHand = Mix(handUp, insert, SmoothStep(per));
				}
				else if(reload < 1.4f){
				
					float per = (reload - 1.0f) / 0.4f;
					rightHand = Mix(insert, boltBack, SmoothStep(per));
				}
				else if(reload < 1.8f){
				
					float per = (reload - 1.2f) / 0.8f;
					rightHand = Mix(boltBack, bolt, SmoothStep(per));
				}
				else if(reload < 2.4f){
				
					float per = (reload - 2.0f) / 0.8f;
					rightHand = Mix(bolt, rightHand, SmoothStep(per));
				}

			}	
			
			param.matrix = eyeMatrix * mat;

			LeftHandPosition = leftHand;
			RightHandPosition = rightHand;
			
			//Bolt Handle
			param.matrix = weapMatrix * CreateTranslateMatrix(0.f, -6.8f, -3.2f) * CreateScaleMatrix(0.056f);
			param.depthHack = true;
			if(reloading){ 
			
				if(reload < 0.21f){

					float per = (reload - 0.01f) / 0.2f;
				
				}
				else if(reload < .5f){
				
					float per = (reload - 0.12f) / 0.09f;
					param.matrix *= CreateTranslateMatrix(0.f, per*-25.f, 0.f);//-18
				
		
				}	
				else if(reload < 1.1f){
					param.matrix *= CreateTranslateMatrix(0.f, -100.f, 0.f);		
					
				}
				else if(reload < 1.2f){
					param.matrix *= CreateTranslateMatrix(0.f, -100.f, 0.f);		
					
				}	
				else if(reload < 1.3f){
					param.matrix *= CreateTranslateMatrix(0.f, -100.f, 0.f);	
					
				}
                else if(reload < 1.4f){
					param.matrix *= CreateTranslateMatrix(0.f, -100.f, 0.f);		
					
				}
				else if(reload < 1.5f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-23.f, 0.f);
				}
                else if(reload < 1.51f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-22.f, 0.f);
				}
                
				else if(reload < 1.52f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-21.f, 0.f);
				}
				else if(reload < 1.53f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-20.f, 0.f);
				}
				else if(reload < 1.54f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-19.f, 0.f);
				}
				else if(reload < 1.55f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-18.f, 0.f);
				}
				else if(reload < 1.56f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-17.f, 0.f);
				}
				else if(reload < 1.57f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-16.f, 0.f);
				}
				else if(reload < 1.58f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-15.f, 0.f);
				}
				else if(reload < 1.59f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-14.f, 0.f);
				}
				else if(reload < 1.60f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-13.f, 0.f);
				}
				else if(reload < 1.61f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-12.f, 0.f);
	
				}
				else if(reload < 1.62f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-11.f, 0.f);
				}
				else if(reload < 1.63f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-10.f, 0.f);
				}
				else if(reload < 1.64f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-9.f, 0.f);
				}
				else if(reload < 1.65f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-8.f, 0.f);
				}
				else if(reload < 1.66f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-7.f, 0.f);
				}
				else if(reload < 1.67f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-6.f, 0.f);
				}
				else if(reload < 1.68f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-5.f, 0.f);
				}
				else if(reload < 1.69f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-4.f, 0.f);
				}
				else if(reload < 1.70f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-3.f, 0.f);
				}
				else if(reload < 1.71f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-2.f, 0.f);
				}
				else if(reload < 1.72f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*-1.f, 0.f);
				}
				else if(reload < 1.73f){   
					float per = (reload + 2.5f) / 1.f;
					param.matrix *= CreateTranslateMatrix(0.f,  per*0.f, 0.f);
				}

			}
			
			renderer.AddModel(boltmodel, param);
			
			//Magazine
			param.matrix = weapMatrix * CreateTranslateMatrix(-0.05f, -4.8f, -0.5f) * CreateScaleMatrix(0.038f);
			param.depthHack = true;
			if(reloading){
				if(reload < 1.0f){
					float per = (reload) / 0.5f;
				}
				if(reload < 0.9f){
					float per = (reload - 1.0f) / .5f;
					param.matrix *= CreateTranslateMatrix(-35.f, 0.f, 0.f);
					param.matrix *= CreateTranslateMatrix(0.f, per*220.f, per*-24.f);
				}
				else if(reload < 1.f){
					param.matrix *= CreateTranslateMatrix(0.f, 0.f, -50.f);
				}
				else if(reload < 1.05f){
					float per = (reload - 1.8f) / 0.8f;
					param.matrix *= CreateTranslateMatrix(0.f, 0.f, per*50.f);
				}
				else if(reload < 1.4f){
					float per = (reload - 2.2f) / 0.3f;
					param.matrix *= CreateTranslateMatrix(0.f, 0.f, 0.f);		
				}
			
			}	
			
			renderer.AddModel(magazineModel, param);
			
		}
	}

	IWeaponSkin@ CreateViewRifleSkin(Renderer@ r, AudioDevice@ dev) {
		return ViewRifleSkin(r, dev);
	}
}
