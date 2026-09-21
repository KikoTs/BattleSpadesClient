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
		private Model@ magazineModel;
		private Model@ scopeModel1;
		private Model@ scopeModel2;
		private Model@ movemodel2;
		
		private Image@ reflexImage;
		
		private AudioChunk@ fireSounds;
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@ reloadSound;
		
		ViewRifleSkin(Renderer@ r, AudioDevice@ dev){
			super(r);
			@audioDevice = dev;
			@gunModel = renderer.RegisterModel
				("Models/Weapons/Rifle/WeaponNoMagazine.kv6");
			@magazineModel = renderer.RegisterModel
				("Models/Weapons/Rifle/Magazine.kv6");
			@scopeModel1 = renderer.RegisterModel
				("Models/Weapons/Rifle/Scope1.kv6");
			@scopeModel2 = renderer.RegisterModel
				("Models/Weapons/Rifle/Scope2.kv6");
			@movemodel2 = renderer.RegisterModel
				("Models/Weapons/Rifle/WeaponMove2.kv6");
				
			@reflexImage = renderer.RegisterImage
				("Gfx/RifleSight.png");
				
			@fireSounds = dev.RegisterSound
				("Sounds/Weapons/Rifle/FireLocal.wav");
			@fireFarSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/FireFar.wav");
			@fireStereoSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/FireStereo.wav");
			@reloadSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/ReloadLocal.wav");
				
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
				audioDevice.PlayLocal(fireSounds, origin, param);
				param.volume = 0.7f;
				audioDevice.PlayLocal(fireFarSound, origin, param);
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
		
		//ATTENTION
		//do you see "AimDownSightStateSmooth * #.###f;"? the # tells how high is the gun when aimed.
		//so if you increase it, the gun will be lifted higher when aiming. (edit: probably, something's strange here)
		float GetZPos() {
			return 0.2f - AimDownSightStateSmooth * 0.02f;
		}
		
		// rotates gun matrix to ensure the sight is in
		// the center of screen (0, ?, 0).
		// backported for 0.0.9.
		Matrix4 AdjustToAlignSight(Matrix4 mat, Vector3 sightPos, float fade) 
		{
			Vector3 p = mat * sightPos;
			mat = CreateRotateMatrix(Vector3(0.f, 0.f, -1.f), atan(p.x / p.y) * fade) * mat;
			//mat = CreateRotateMatrix(Vector3(1.f, 0.f, 0.f), atan(p.z / p.y) * fade) * mat; //this line is fucking bullshit
			return mat * CreateTranslateMatrix(0.f, 0.f, fade*-1.5f);
		}
		//OLD STUFF
		/*Matrix4 AdjustToAlignSight(Matrix4 mat, Vector3 sightPos, float fade) {
			Vector3 p = mat * sightPos;
			float x = Dot(p, Vector3(1.f, 0.f, 0.f));
			float y = Dot(p, Vector3(0.f, 1.f, 0.f));
			float z = Dot(p, Vector3(0.f, 0.f, 1.f));
			mat = CreateRotateMatrix(Vector3(0.f, 0.f, 1.f), atan(x / y) * fade) * mat;
			mat = CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), atan(z / y) * fade) * mat;
			return mat;
		}*/
		//OLD STUFF
		
		void Draw2D() {
			if(AimDownSightState > 0.7)
				return;
			BasicViewWeapon::Draw2D();
		}
		
		//(x,y,z) (x+left/-right, y+further/-closer, z+down/-up)
		void AddToScene() {
			Matrix4 mat = CreateScaleMatrix(0.033f);
			mat = GetViewWeaponMatrix() * mat;
			
			bool reloading = IsReloading;
			float reload = ReloadProgress;
			Vector3 leftHand, leftHando, rightHand;
			Vector3 leftHand2, leftHand3, leftHand4;
			Vector3 rightHand2, rightHand3;
			
			reload *= 2.5f;
			
			//
//Adjustion of gun height when aiming
			//
			//float per = AimDownSightStateSmooth;
			//mat= CreateTranslateMatrix(0.f, 0.f, per*5.f);
			
			//
//NEW ANIMATION
			//
			//THIS IS THE PART WHERE THE GUN IS ROLLED
			//hence "CreateRotateMatrix"
			if(reloading)
			{
				if(reload < 0.2f)
				{
					float per = reload / 0.2f;
					if (per < 0.8f)
					{
						mat *= CreateRotateMatrix(Vector3(0.f, 1.f, 0.f), per*0.375f); //>roll =0.3
						//
						mat *= CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), per*0.25f); //>lift =-0.2
					}
					else
					{
						mat *= CreateRotateMatrix(Vector3(0.f, 1.f, 0.f), 0.3f);	//roll =0.3
						//
						mat *= CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), 0.2f);	//lift =-0.2
					}
				}
				else if(reload < 0.4f) //removing magazine (move down)
				{
					float per = (reload - 0.2f) / 0.2f;
					mat *= CreateRotateMatrix(Vector3(0.f, 1.f, 0.f), 0.3f);		//roll =0.3
					//
					mat *= CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), 0.2f);		//lift 
					mat *= CreateRotateMatrix(Vector3(1.f, 0.f, 0.f), per*0.1f);	//>lift =-0.1
					//
					mat *= CreateTranslateMatrix(0.f, 0.f, per*2.f);				//>raise =2.0
				}
				else if(reload < 0.7f) //recovers from moving down
				{
					float per = (reload - 0.4f) / 0.3f;
					mat *= CreateRotateMatrix(Vector3(0.f, 1.f, 0.f), 0.3f);		//roll =0.3
					//
					mat *= CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), 0.1f);		//lift
					mat *= CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), per*0.1f);	//>lift =-0.2
					//
					mat *= CreateTranslateMatrix(0.f, 0.f, 2.f);					//raise
					mat *= CreateTranslateMatrix(0.f, 0.f, per*-0.6f);				//>raise =1.4
				}
				else if(reload < 1.2f) //idling
				{
					mat *= CreateRotateMatrix(Vector3(0.f, 1.f, 0.f), 0.3f);
					//
					mat *= CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), 0.2f);
					//
					mat *= CreateTranslateMatrix(0.f, 0.f, 1.4f);
				}
				else if(reload < 1.5f) //inserting magazine (move up)
				{
					float per = (reload - 1.2f) / 0.3f;
					mat *= CreateRotateMatrix(Vector3(0.f, 1.f, 0.f), 0.3f);		//roll
					mat *= CreateRotateMatrix(Vector3(0.f, -1.f, 0.f), per*0.2f);	//>roll =0.1
					//
					mat *= CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), 0.2f);		//lift
					mat *= CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), per*0.1f);	//>lift =-0.3
					//
					mat *= CreateTranslateMatrix(0.f, 0.f, 1.4f);					//raise
					mat *= CreateTranslateMatrix(0.f, 0.f, per*-1.4f);				//>raise =0					
				}
				else if(reload < 2.f) //rolls left, prepares for bolt
				{
					float per = (reload - 1.5f) / 0.5f;	
					mat *= CreateRotateMatrix(Vector3(0.f, 1.f, 0.f), 0.1f);		//roll
					mat *= CreateRotateMatrix(Vector3(0.f, -1.f, 0.f), per*0.2f);	//>roll =-0.1
					//
					mat *= CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), 0.3f);		//lift
					mat *= CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), per*0.1f);	//>lift	=-0.4
				}
				else if(reload < 2.2f) //bolt action (moves up)
				{
					float per = (reload - 2.f) / 0.2f;	
					mat *= CreateRotateMatrix(Vector3(0.f, -1.f, 0.f), 0.1f);			//roll 
					mat *= CreateRotateMatrix(Vector3(0.f, -1.f, 0.f), per*0.2f);	//>roll =-0.3
					//
					mat *= CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), 0.4f);		//lift
					mat *= CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), per*0.4f);	//>lift	=-0.8
				}
				else if(reload < 2.4f) //bolt action>original
				{
					float per = (reload - 2.2f) / 0.2f;
					mat *= CreateRotateMatrix(Vector3(0.f, -1.f, 0.f), 0.3f);		//roll 
					mat *= CreateRotateMatrix(Vector3(0.f, 1.f, 0.f), per*0.3f);	//>roll =0
					//
					mat *= CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), 0.8f);		//lift
					mat *= CreateRotateMatrix(Vector3(1.f, 0.f, 0.f), per*0.5f);	//>lift	=-0.3
				}
				else //going to original
				{
					float per = (reload - 2.4f) / 0.1f;
					mat *= CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), 0.3f);		//lift
					mat *= CreateRotateMatrix(Vector3(1.f, 0.f, 0.f), per*0.3f);	//>lift	=0
				}
			}
			//
//END OF NEW ANIMATION
			//
			rightHand = mat * Vector3(-0.5f, -7.25f, 3.f);
			leftHand = mat * Vector3(2.f, 7.f, 1.f);
			leftHando = mat * Vector3(2.5f, 6.5f, 2.f);
			
			rightHand2 = mat * Vector3(-0.75f, -50.0f, 3.5f);
			rightHand3 = mat * Vector3(-0.75f, -100.0f, 4.f);
			
			leftHand2 = mat * Vector3(1.75f, -10.f, 4.f);//removing magazine
			leftHand3 = mat * Vector3(3.5f, 4.f, 0.5f);//grab charging handle
			leftHand4 = mat * Vector3(3.5f, 1.f, 0.5f);//pull charigng handle
			//
//NEW ANIMATION for HANDS (for now righthand position bugfix - moves the hand out of the screen)
			//
			//(x,y,z) (x+left/-right, y+further/-closer, z+down/-up)
			//if(reloading)
			//{
			//	rightHand = mat * Vector3(0.f, -10.f, 100.f);
			//}
			
			// stick the scope to the center of screen
			if(AimDownSightStateSmooth > 0.8f){
				mat = AdjustToAlignSight(mat, Vector3(0.f, -8.f, -5.75f), (AimDownSightStateSmooth - 0.8f) / 0.2f);
			}
			
			ModelRenderParam param;
			Matrix4 weapMatrix = eyeMatrix * mat * CreateScaleMatrix(0.5f) * CreateTranslateMatrix(0.f, 0.f, -1.5f);
			//weapMatrix is very useful, because it is not modified anywhere else. it means that it can be used as a basis for other models
			//whithout the need to rewrite this whole line above.
			param.matrix = weapMatrix;
			param.depthHack = true;
			renderer.AddModel(gunModel, param);
			
			// draw sights
			Matrix4 sightMat = weapMatrix;
			sightMat *= CreateTranslateMatrix(0.f, -7.25f, -8.25f);
			sightMat *= CreateScaleMatrix(0.25f, 0.5f, 0.25f); //1/6 scale is not good... better 1/5 or 1/10 or 1/8 so you dont get 0.16666666
			param.matrix = sightMat;
			renderer.AddModel(scopeModel1, param);
			
			sightMat = weapMatrix;
			sightMat *= CreateTranslateMatrix(0.f, -7.25f, -8.25f);
			sightMat *= CreateScaleMatrix(sqrt(2)/4, 0.5f, sqrt(2)/4); //1/6 scale is not good... better 1/5 or 1/10 or 1/8 so you dont get 0.16666666
			sightMat *= CreateRotateMatrix(Vector3(0.f, 1.f, 0.f), atan(1));
			param.matrix = sightMat;
			renderer.AddModel(scopeModel2, param); //DIAGONAL SCOPE MODEL - it is rotated counter clockwise
			
			// draw reflex image
			float reflexOpacity = (AimDownSightState > 0.8f) ? AimDownSightState * 5.f - 4.f : 0.f;
			Vector3 reflexPos = eyeMatrix * Vector3(0.f, 0.2f, 0.f);
			renderer.Color = Vector4(reflexOpacity, reflexOpacity, reflexOpacity, 0.f); // premultiplied alpha
			renderer.AddLongSprite(reflexImage, reflexPos, reflexPos, 0.004f);
			
			// magazine/reload action
			mat *= CreateTranslateMatrix(0.f, 0.f, 0.f);
			if(reloading) 
			{
				if(reload < 0.7f)
				{
					// magazine release, moving down
					float per = reload / 0.7f;
					mat *= CreateTranslateMatrix(0.f, 0.f, per*per*50.f);
					leftHand = Mix(leftHand, leftHand2, SmoothStep(per));
					rightHand = rightHand3;
				}
				else if(reload < 1.4f) 
				{
					// insert magazine, idling
					float per = (1.4f - reload) / 0.7f;
					if(per < 0.3f) 
					{
						// non-smooth insertion
						per *= 4.f; per -= 0.4f;
						per = Clamp(per, 0.0f, 0.3f);
					}
					
					mat *= CreateTranslateMatrix(0.f, 0.f, per*per*10.f);
					leftHand = mat * Vector3(0.f, 0.f, 4.f);
					rightHand = rightHand3;
					if (reload >= 1.2f)
					//moving up
					{
						per = (reload - 1.2f) / 0.3f;
						leftHand = Mix(leftHand, leftHando, SmoothStep(sqrt(per)));
					}
				}
				else if(reload < 1.5f) //shortly moving up after inserting magazine
				{
					float per = (reload - 1.4f) / 0.1f;
					//>roll =0.1 (rolled right, rolling left +(-0.2))
					//>lift =-0.3 (rotated up, rotating up +(-0.1))
					//>raise =0 (height zeroed)
					rightHand = rightHand3;
					leftHand = leftHando;
				}
				else if(reload < 2.f) //rolls left, prepares for bolt
				{
					float per = (reload - 1.5f) / 0.5f;	
					//>roll =-0.1
					//>lift	=-0.4
					rightHand = rightHand3;
					leftHand = Mix(leftHando, leftHand3, SmoothStep(per));
				}
				else if(reload < 2.2f) //bolt action (rotates up)
				{
					float per = (reload - 2.f) / 0.2f;	
					//>roll =-0.3
					//>lift	=-0.8
					rightHand = rightHand3;
					leftHand = Mix(leftHand3, leftHand4, SmoothStep(per));
				}
				else if(reload < 2.4f) //bolt action>original
				{
					float per = (reload - 2.2f) / 0.2f;
					//>roll =0
					//>lift	=-0.3
					rightHand = rightHand3;
					leftHand = Mix(leftHand4, leftHand, SmoothStep(per));
				}
				else //going to original
				{
					float per = (reload - 2.4f) / 0.1f;
					//>lift	=0
					rightHand = rightHand;
				}
			}
			//adds magazine model. below I made it so magaine and WeaponNoMagazine have the SAME parameters
			//(except that Magazine moves when reloading)
			//so I just added a magazine to WeaponNoMagazine.kv6, erased only the weapon and saved as Magazine.kv6
			//so it fits like a glove. same was done with WEAPONMOVE2
			param.matrix = eyeMatrix * mat * CreateScaleMatrix(0.5f) * CreateTranslateMatrix(0.f, 0.f, -1.5f);
			renderer.AddModel(magazineModel, param);

			//WEAPONMOVE2-(COCKING-HANDLE)-------------------------------------------------------#-#-#-#-#
			//WEAPONMOVE1 was deleted - it is not visible under any conditions
			param.matrix = weapMatrix * CreateTranslateMatrix(-0.1f, 0.f, 0.f);
			param.depthHack = true;
			if(reloading) 
			{
				if(reload < 1.9f)
				{
				}
				else if(reload < 2.3f)
				{
					float per = (reload - 1.9f) / 0.4f;
					param.matrix *= CreateTranslateMatrix(0.f, per*-5.f, 0.f);
				}
				else if(reload < 2.31f) 
				{
				//the cocking handle moves nowhere, just idles before going back to original position
				param.matrix *= CreateTranslateMatrix(0.f, -5.f, 0.f);
				}
				else if(reload < 2.4f)
				{
				//the c*cking handle now goes back to original position. 
				//you can erase the line immediately below and see why it's here
					param.matrix *= CreateTranslateMatrix(0.f, -5.f, 0.f);		
					float per = (reload - 2.31f) / 0.09f;
					param.matrix *= CreateTranslateMatrix(0.f, per*5.f, 0.f);
				}
			}
			//finally tells game to add a model and tells the name of variable it should use
			renderer.AddModel(movemodel2, param);
			//END OF WEAPONMOVE2
			
			LeftHandPosition = leftHand;
			RightHandPosition = rightHand;
		}
	}
	
	IWeaponSkin@ CreateViewRifleSkin(Renderer@ r, AudioDevice@ dev) {
		return ViewRifleSkin(r, dev);
	}
}
