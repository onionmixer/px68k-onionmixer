// JOYSTICK.C - joystick support for WinX68k

#include "common.h"
#include "prop.h"
#include "joystick.h"
#include "winui.h"
#include "keyboard.h"
#ifdef PSP
#include <pspctrl.h>
#else
#include <SDL.h>
#endif
#if defined(ANDROID) || TARGET_OS_IPHONE || defined(PSP)
#include "mouse.h"
#endif

#ifndef MAX_BUTTON
#define MAX_BUTTON 32
#endif

char joyname[2][MAX_PATH];
char joybtnname[2][MAX_BUTTON][MAX_PATH];
BYTE joybtnnum[2] = {0, 0};

BYTE joy[2];
BYTE JoyKeyState;
BYTE JoyKeyState0;
BYTE JoyKeyState1;
BYTE JoyState0[2];
BYTE JoyState1[2];

// This stores whether the buttons were down. This avoids key repeats.
BYTE JoyDownState0;
BYTE MouseDownState0;

// This stores whether the buttons were up. This avoids key repeats.
BYTE JoyUpState0;
BYTE MouseUpState0;

#ifdef PSP
DWORD JoyDownStatePSP;
BYTE JoyAnaPadX;
BYTE JoyAnaPadY;
static void mouse_update_psp(SceCtrlData psppad);
#endif
BYTE JoyPortData[2];

#if defined(ANDROID) || TARGET_OS_IPHONE

#define VBTN_MAX 8
#define VBTN_NOUSE 0
#define FINGER_MAX 10

// The value is (0..1.0), which compares with SDL_FINGER.
typedef struct _vbtn_rect {
	float x;
	float y;
	float x2;
	float y2;
} VBTN_RECT;

VBTN_RECT vbtn_rect[VBTN_MAX];
BYTE vbtn_state[VBTN_MAX];
SDL_TouchID touchId = -1;

#define SET_VBTN(id, bx, by, s)					\
{								\
	vbtn_state[id] = VBTN_OFF;				\
	vbtn_rect[id].x = (float)(bx) / 800.0;			\
	vbtn_rect[id].y = (float)(by) / 600.0;			\
	vbtn_rect[id].x2 = ((float)(bx) + 32.0*(s)) / 800.0;	\
	vbtn_rect[id].y2 = ((float)(by) + 32.0*(s)) / 600.0;	\
}

// base points of the virtual keys
VBTN_POINTS vbtn_points[] = {
	{20, 450}, {100, 450}, {60, 400}, {60, 500}, // d-pad
	{680, 450}, {750, 450}, // pad button
	{768, 0}, // software keyboard button
	{768, 52} // menu button
};

// fixed coordinates, which are fixed with any scalings.
#define VKEY_L_X 20 // x of the left d-pad key
#define VKEY_D_Y 532 // y of the base of the down d-pad key
#define VKEY_R_X 782 // x of the right side of the right pad button
#define VKEY_K_X 800 // x of the right side of keyboard button

// correction value for fixing keys
#define VKEY_DLX(scale) (VKEY_L_X * (scale) - VKEY_L_X)
#define VKEY_DDY(scale) (VKEY_D_Y * (scale) - VKEY_D_Y)
#define VKEY_DRX(scale) (VKEY_R_X * (scale) - VKEY_R_X)
#define VKEY_DKX(scale) (VKEY_K_X * (scale) - VKEY_K_X)

VBTN_POINTS scaled_vbtn_points[sizeof(vbtn_points)/sizeof(VBTN_POINTS)];

VBTN_POINTS *Joystick_get_btn_points(float scale)
{
	int i;

	for (i = 0; i < 4; i++) {
		scaled_vbtn_points[i].x
			= -VKEY_DLX(scale) + scale * vbtn_points[i].x;
		scaled_vbtn_points[i].y
			= -VKEY_DDY(scale) + scale * vbtn_points[i].y;
	}
	for (i = 4; i < 6; i++) {
		scaled_vbtn_points[i].x
			= -VKEY_DRX(scale) + scale * vbtn_points[i].x;
		scaled_vbtn_points[i].y
			= -VKEY_DDY(scale) + scale * vbtn_points[i].y;
	}

	// keyboard button
	scaled_vbtn_points[i].x
		= -VKEY_DKX(scale) + scale * vbtn_points[i].x;
	scaled_vbtn_points[i].y
		= 0;

	i++;

	// menu button
	scaled_vbtn_points[i].x
		= -VKEY_DKX(scale) + scale * vbtn_points[i].x;
	scaled_vbtn_points[i].y
		= 0 + scale * vbtn_points[i].y;

	return scaled_vbtn_points;
}

void Joystick_Vbtn_Update(float scale)
{
	int i;
	VBTN_POINTS *p;

	p = Joystick_get_btn_points(scale);

	for (i = 0; i < VBTN_MAX; i++) {
		vbtn_state[i] = VBTN_NOUSE;
		//p6logd("id: %d x: %f y: %f", i, p->x, p->y);
		SET_VBTN(i, p->x, p->y, scale);
		p++;
	}
}

BYTE Joystick_get_vbtn_state(WORD n)
{
	return vbtn_state[n];
}

#endif

#ifndef PSP
SDL_Joystick *sdl_joy[MAX_JOYSTICKS];
SDL_GameController *sdl_gamecontroller[MAX_JOYSTICKS];
#endif

void Joystick_Init(void)
{
#ifndef PSP
	int i, j, nr_joys, nr_axes, nr_btns, nr_hats;
	int joy_index = 0;
#endif

	joy[0] = 1;  // active
	joy[1] = 1;  // active (2P support)
	JoyKeyState = 0;
	JoyKeyState0 = 0;
	JoyKeyState1 = 0;
	JoyState0[0] = 0xff;
	JoyState0[1] = 0xff;
	JoyState1[0] = 0xff;
	JoyState1[1] = 0xff;
	JoyPortData[0] = 0;
	JoyPortData[1] = 0;

#if defined(ANDROID) || TARGET_OS_IPHONE
	Joystick_Vbtn_Update(WinUI_get_vkscale());
#endif

#ifndef PSP
	for (j = 0; j < MAX_JOYSTICKS; j++) {
		sdl_joy[j] = NULL;
		sdl_gamecontroller[j] = NULL;
	}

	SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);

	nr_joys = SDL_NumJoysticks();
	p6logd("joy num %d\n", nr_joys);

	// Open up to MAX_JOYSTICKS joysticks
	for (i = 0; i < nr_joys && joy_index < MAX_JOYSTICKS; i++) {
		// Try GameController API first (provides standardized mapping)
		if (SDL_IsGameController(i)) {
			sdl_gamecontroller[joy_index] = SDL_GameControllerOpen(i);
			if (sdl_gamecontroller[joy_index]) {
				p6logd("GameController[%d]: %s\n", joy_index, SDL_GameControllerName(sdl_gamecontroller[joy_index]));
				// Get underlying joystick for compatibility
				sdl_joy[joy_index] = SDL_GameControllerGetJoystick(sdl_gamecontroller[joy_index]);
				joy_index++;
				continue;
			}
		}

		// Fall back to Joystick API
		sdl_joy[joy_index] = SDL_JoystickOpen(i);
		if (sdl_joy[joy_index]) {
			nr_btns = SDL_JoystickNumButtons(sdl_joy[joy_index]);
			nr_axes = SDL_JoystickNumAxes(sdl_joy[joy_index]);
			nr_hats = SDL_JoystickNumHats(sdl_joy[joy_index]);

			p6logd("Joystick[%d]: %s\n", joy_index, SDL_JoystickNameForIndex(i));
			p6logd("# of Axes: %d\n", nr_axes);
			p6logd("# of Btns: %d\n", nr_btns);
			p6logd("# of Hats: %d\n", nr_hats);

			// skip accelerometer and keyboard
			if (nr_btns < 2 || (nr_axes < 2 && nr_hats == 0)) {
				SDL_JoystickClose(sdl_joy[joy_index]);
				sdl_joy[joy_index] = NULL;
			} else {
				joy_index++;
			}
		} else {
			p6logd("can't open joy %d\n", i);
		}
	}

	p6logd("Total joysticks opened: %d\n", joy_index);
#endif
}

void Joystick_Cleanup(void)
{
#ifndef PSP
	int i;
	for (i = 0; i < MAX_JOYSTICKS; i++) {
		if (sdl_gamecontroller[i]) {
			SDL_GameControllerClose(sdl_gamecontroller[i]);
			sdl_gamecontroller[i] = NULL;
			sdl_joy[i] = NULL;  // Owned by GameController, already closed
		} else if (sdl_joy[i] && SDL_JoystickGetAttached(sdl_joy[i])) {
			SDL_JoystickClose(sdl_joy[i]);
			sdl_joy[i] = NULL;
		}
	}
#endif
}

BYTE FASTCALL Joystick_Read(BYTE num)
{
	BYTE joynum = num;
	BYTE ret0 = 0xff, ret1 = 0xff, ret;

	if (Config.JoySwap) joynum ^= 1;
	if (joy[num]) {
		ret0 = JoyState0[num];
		ret1 = JoyState1[num];
	}

	if (Config.JoyKey)
	{
		if ((Config.JoyKeyJoy2)&&(num==1))
			ret0 ^= JoyKeyState;
		else if ((!Config.JoyKeyJoy2)&&(num==0))
			ret0 ^= JoyKeyState;
	}

	ret = ((~JoyPortData[num])&ret0)|(JoyPortData[num]&ret1);

	return ret;
}


void FASTCALL Joystick_Write(BYTE num, BYTE data)
{
	if ( (num==0)||(num==1) ) JoyPortData[num] = data;
}

#ifndef PSP
// Helper function to read input from a single gamecontroller/joystick
static void read_joystick_input(int joy_num, BYTE *ret0, BYTE *ret1)
{
	signed int x, y;
	UINT8 hat;

	*ret0 = 0xff;
	*ret1 = 0xff;

	if (sdl_gamecontroller[joy_num]) {
		// Use SDL GameController API (standardized mapping)
		x = SDL_GameControllerGetAxis(sdl_gamecontroller[joy_num], SDL_CONTROLLER_AXIS_LEFTX);
		y = SDL_GameControllerGetAxis(sdl_gamecontroller[joy_num], SDL_CONTROLLER_AXIS_LEFTY);

		if (x < -JOYAXISPLAY) {
			*ret0 ^= JOY_LEFT;
		}
		if (x > JOYAXISPLAY) {
			*ret0 ^= JOY_RIGHT;
		}
		if (y < -JOYAXISPLAY) {
			*ret0 ^= JOY_UP;
		}
		if (y > JOYAXISPLAY) {
			*ret0 ^= JOY_DOWN;
		}

		// D-pad buttons
		if (SDL_GameControllerGetButton(sdl_gamecontroller[joy_num], SDL_CONTROLLER_BUTTON_DPAD_UP)) {
			*ret0 ^= JOY_UP;
		}
		if (SDL_GameControllerGetButton(sdl_gamecontroller[joy_num], SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
			*ret0 ^= JOY_DOWN;
		}
		if (SDL_GameControllerGetButton(sdl_gamecontroller[joy_num], SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
			*ret0 ^= JOY_LEFT;
		}
		if (SDL_GameControllerGetButton(sdl_gamecontroller[joy_num], SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
			*ret0 ^= JOY_RIGHT;
		}

		// Face buttons: A/B for TRG1/TRG2, X/Y for TRG3/TRG4
		if (SDL_GameControllerGetButton(sdl_gamecontroller[joy_num], SDL_CONTROLLER_BUTTON_A)) {
			*ret0 ^= JOY_TRG1;
		}
		if (SDL_GameControllerGetButton(sdl_gamecontroller[joy_num], SDL_CONTROLLER_BUTTON_B)) {
			*ret0 ^= JOY_TRG2;
		}
		if (SDL_GameControllerGetButton(sdl_gamecontroller[joy_num], SDL_CONTROLLER_BUTTON_X)) {
			*ret1 ^= JOY_TRG3;
		}
		if (SDL_GameControllerGetButton(sdl_gamecontroller[joy_num], SDL_CONTROLLER_BUTTON_Y)) {
			*ret1 ^= JOY_TRG4;
		}
		// Shoulder buttons: LB/RB for TRG5/TRG6
		if (SDL_GameControllerGetButton(sdl_gamecontroller[joy_num], SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) {
			*ret1 ^= JOY_TRG5;
		}
		if (SDL_GameControllerGetButton(sdl_gamecontroller[joy_num], SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
			*ret1 ^= JOY_TRG6;
		}
		// Triggers: LT/RT for TRG7/TRG8
		if (SDL_GameControllerGetAxis(sdl_gamecontroller[joy_num], SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16384) {
			*ret1 ^= JOY_TRG7;
		}
		if (SDL_GameControllerGetAxis(sdl_gamecontroller[joy_num], SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16384) {
			*ret1 ^= JOY_TRG8;
		}
	} else if (sdl_joy[joy_num]) {
		// Fallback to raw Joystick API
		x = SDL_JoystickGetAxis(sdl_joy[joy_num], Config.HwJoyAxis[0]);
		y = SDL_JoystickGetAxis(sdl_joy[joy_num], Config.HwJoyAxis[1]);

		if (x < -JOYAXISPLAY) {
			*ret0 ^= JOY_LEFT;
		}
		if (x > JOYAXISPLAY) {
			*ret0 ^= JOY_RIGHT;
		}
		if (y < -JOYAXISPLAY) {
			*ret0 ^= JOY_UP;
		}
		if (y > JOYAXISPLAY) {
			*ret0 ^= JOY_DOWN;
		}

		hat = SDL_JoystickGetHat(sdl_joy[joy_num], Config.HwJoyHat);

		if (hat) {
			switch (hat) {
			case SDL_HAT_RIGHTUP:
				*ret0 ^= JOY_RIGHT;
				/* fall through */
			case SDL_HAT_UP:
				*ret0 ^= JOY_UP;
				break;
			case SDL_HAT_RIGHTDOWN:
				*ret0 ^= JOY_DOWN;
				/* fall through */
			case SDL_HAT_RIGHT:
				*ret0 ^= JOY_RIGHT;
				break;
			case SDL_HAT_LEFTUP:
				*ret0 ^= JOY_UP;
				/* fall through */
			case SDL_HAT_LEFT:
				*ret0 ^= JOY_LEFT;
				break;
			case SDL_HAT_LEFTDOWN:
				*ret0 ^= JOY_LEFT;
				/* fall through */
			case SDL_HAT_DOWN:
				*ret0 ^= JOY_DOWN;
				break;
			}
		}

		if (SDL_JoystickGetButton(sdl_joy[joy_num], Config.HwJoyBtn[0])) {
			*ret0 ^= JOY_TRG1;
		}
		if (SDL_JoystickGetButton(sdl_joy[joy_num], Config.HwJoyBtn[1])) {
			*ret0 ^= JOY_TRG2;
		}
		if (SDL_JoystickGetButton(sdl_joy[joy_num], Config.HwJoyBtn[2])) {
			*ret1 ^= JOY_TRG3;
		}
		if (SDL_JoystickGetButton(sdl_joy[joy_num], Config.HwJoyBtn[3])) {
			*ret1 ^= JOY_TRG4;
		}
		if (SDL_JoystickGetButton(sdl_joy[joy_num], Config.HwJoyBtn[4])) {
			*ret1 ^= JOY_TRG5;
		}
		if (SDL_JoystickGetButton(sdl_joy[joy_num], Config.HwJoyBtn[5])) {
			*ret1 ^= JOY_TRG6;
		}
		if (SDL_JoystickGetButton(sdl_joy[joy_num], Config.HwJoyBtn[6])) {
			*ret1 ^= JOY_TRG7;
		}
		if (SDL_JoystickGetButton(sdl_joy[joy_num], Config.HwJoyBtn[7])) {
			*ret1 ^= JOY_TRG8;
		}
	}
}
#endif

#ifdef PSP
void FASTCALL Joystick_Update(int is_menu)
#else
void FASTCALL Joystick_Update(int is_menu, SDL_Keycode key)
#endif
{
	BYTE ret0 = 0xff, ret1 = 0xff;
	BYTE ret0_2 = 0xff, ret1_2 = 0xff;  // 2P joystick
	BYTE mret0 = 0xff, mret1 = 0xff;
	static BYTE pre_ret0 = 0xff, pre_mret0 = 0xff;
#if defined(PSP)
	static DWORD button_down = 0;
	DWORD button_changing;

	SceCtrlData psppad;
	sceCtrlPeekBufferPositive(&psppad, 1);

	if (is_menu || !Config.JoyOrMouse || Keyboard_IsSwKeyboard()) {
		if (psppad.Buttons & PSP_CTRL_LEFT) {
			ret0 ^= JOY_LEFT;
		}
		if (psppad.Buttons & PSP_CTRL_RIGHT) {
			ret0 ^= JOY_RIGHT;
		}
		if (psppad.Buttons & PSP_CTRL_UP) {
			ret0 ^= JOY_UP;
		}
		if (psppad.Buttons & PSP_CTRL_DOWN) {
			ret0 ^= JOY_DOWN;
		}
		if (psppad.Buttons & PSP_CTRL_CIRCLE) {
			ret0 ^= JOY_TRG1;
		}
		if (psppad.Buttons & PSP_CTRL_CROSS) {
			ret0 ^= JOY_TRG2;
		}
	} else {
		if (psppad.Buttons & PSP_CTRL_CIRCLE) {
			mret0 ^= JOY_TRG1;
		}
		if (psppad.Buttons & PSP_CTRL_CROSS) {
			mret0 ^= JOY_TRG2;
		}
	}

	JoyDownState0 = ~(ret0 ^ pre_ret0) | ret0;
	JoyUpState0 = (ret0 ^ pre_ret0) & ret0;
	pre_ret0 = ret0;

	MouseDownState0 = ~(mret0 ^ pre_mret0) | mret0;
	MouseUpState0 = (mret0 ^ pre_mret0) & mret0;
	pre_mret0 = mret0;

	// up the bits which changed the states
	button_changing = psppad.Buttons ^ button_down;
	// first down = changing the state & down now
	JoyDownStatePSP = button_changing & psppad.Buttons;
	// invert the bits which changed the states
	button_down ^= button_changing;

	// save the states of Analog pad
	JoyAnaPadX = psppad.Lx;
	JoyAnaPadY = psppad.Ly;

#else //defined(PSP)
#if defined(ANDROID) || TARGET_OS_IPHONE
	SDL_Finger *finger;
	SDL_FingerID fid;
	float fx, fy;
	int i, j;
	float scale, asb_x, asb_y; // play x, play y of a button

	// all active buttons are set to off
	for (i = 0; i < VBTN_MAX; i++) {
		if (vbtn_state[i] != VBTN_NOUSE) {
			vbtn_state[i] = VBTN_OFF;
		}
	}

	if (touchId == -1)
		goto skip_vpad;

	// A play of the button changes size according to scale.
	scale = WinUI_get_vkscale();
	asb_x = (float)20 * scale / 800.0;
	asb_y = (float)20 * scale / 600.0;

	// set the button on, only which is pushed just now
	for (i = 0; i < FINGER_MAX; i++) {
		finger = SDL_GetTouchFinger(touchId, i);
		if (!finger)
			continue;

		fx = finger->x;
		fy = finger->y;

		//p6logd("id: %d x: %f y: %f", i, fx, fy);

		for (j = 0; j < VBTN_MAX; j++) {
			if (vbtn_state[j] == VBTN_NOUSE)
				continue;

			if (vbtn_rect[j].x - asb_x > fx)
				continue;
			if (vbtn_rect[j].x2 + asb_x < fx)
				continue;
			if (vbtn_rect[j].y - asb_y > fy)
				continue;
			if (vbtn_rect[j].y2 + asb_y < fy)
				continue;

			vbtn_state[j] = VBTN_ON;
			// The buttons don't overlap.
			break;
		}
	}

	if (need_Vpad()) {
		if (vbtn_state[0] == VBTN_ON) {
			ret0 ^= JOY_LEFT;
		}
		if (vbtn_state[1] == VBTN_ON) {
			ret0 ^= JOY_RIGHT;
		}
		if (vbtn_state[2] == VBTN_ON) {
			ret0 ^= JOY_UP;
		}
		if (vbtn_state[3] == VBTN_ON) {
			ret0 ^= JOY_DOWN;
		}
		if (vbtn_state[4] == VBTN_ON) {
			ret0 ^= (Config.VbtnSwap == 0)? JOY_TRG1 : JOY_TRG2;
		}
		if (vbtn_state[5] == VBTN_ON) {
			ret0 ^= (Config.VbtnSwap == 0)? JOY_TRG2 : JOY_TRG1;
		}
	} else if (Config.JoyOrMouse) {
		if (vbtn_state[4] == VBTN_ON) {
			mret0 ^= (Config.VbtnSwap == 0)? JOY_TRG1 : JOY_TRG2;
		}
		if (vbtn_state[5] == VBTN_ON) {
			mret0 ^= (Config.VbtnSwap == 0)? JOY_TRG2 : JOY_TRG1;
		}
	}

skip_vpad:

#endif //defined(ANDROID) || TARGET_OS_IPHONE

	// Update SDL joystick/gamecontroller states once
	SDL_GameControllerUpdate();
	SDL_JoystickUpdate();

	// Read 1P joystick (joy_index 0)
	read_joystick_input(0, &ret0, &ret1);

	// Read 2P joystick (joy_index 1)
	read_joystick_input(1, &ret0_2, &ret1_2);

	// scan keycode for menu UI
	if (key != SDLK_UNKNOWN) {
		switch (key) {
		case SDLK_UP :
			ret0 ^= JOY_UP;
			break;
		case SDLK_DOWN:
			ret0 ^= JOY_DOWN;
			break;
		case SDLK_LEFT:
			ret0 ^= JOY_LEFT;
			break;
		case SDLK_RIGHT:
			ret0 ^= JOY_RIGHT;
			break;
		case SDLK_RETURN:
			ret0 ^= JOY_TRG1;
			break;
		case SDLK_ESCAPE:
			ret0 ^= JOY_TRG2;
			break;
		}
	}

	JoyDownState0 = ~(ret0 ^ pre_ret0) | ret0;
	JoyUpState0 = (ret0 ^ pre_ret0) & ret0;
	pre_ret0 = ret0;

	MouseDownState0 = ~(mret0 ^ pre_mret0) | mret0;
	MouseUpState0 = (mret0 ^ pre_mret0) & mret0;
	pre_mret0 = mret0;

#endif //defined(PSP)
	// disable Joystick when software keyboard is active
	if (!is_menu && !Keyboard_IsSwKeyboard()) {
		JoyState0[0] = ret0;
		JoyState1[0] = ret1;
		JoyState0[1] = ret0_2;
		JoyState1[1] = ret1_2;
	}

#if defined(USE_OGLES11) || defined(PSP)
	// update the states of the mouse buttons
	if (!(MouseDownState0 & JOY_TRG1) | (MouseUpState0 & JOY_TRG1)) {
		printf("mouse btn1 event\n");
		Mouse_Event(1, (MouseUpState0 & JOY_TRG1)? 0 : 1.0, 0);
	}
	if (!(MouseDownState0 & JOY_TRG2) | (MouseUpState0 & JOY_TRG2)) {
		printf("mouse btn2 event\n");
		Mouse_Event(2, (MouseUpState0 & JOY_TRG2)? 0 : 1.0, 0);
	}
#ifdef PSP
	mouse_update_psp(psppad);
#endif

#endif
}

BYTE get_joy_downstate(void)
{
	return JoyDownState0;
}
void reset_joy_downstate(void)
{
	JoyDownState0 = 0xff;
}
BYTE get_joy_upstate(void)
{
	return JoyUpState0;
}
void reset_joy_upstate(void)
{
	JoyUpState0 = 0x00;
}

#ifdef PSP
DWORD Joystick_get_downstate_psp(DWORD ctrl_bit)
{
	return (JoyDownStatePSP & ctrl_bit);
}	  
void Joystick_reset_downstate_psp(DWORD ctrl_bit)
{
	JoyDownStatePSP ^= ctrl_bit;
}	  

void Joystatic_reset_anapad_psp(void)
{
	JoyAnaPadX = JoyAnaPadY = 128;
}

void _get_anapad_sub(BYTE av, int *v, int max)
{
	// accelerate accroding to an angle of the stick
	if (av > 255 / 2 + 32) {
		(*v)++; // move a little bit
		if (av > 255 - 5) {
			(*v)++; // accelerate
			if (av == 255) {
				*v += 2; // more accelerate
			}
		}
		if (*v > max) {
			*v = max;
		}
	}
	if (av < 255 / 2 - 32) {
		(*v)--;
		if (av < 5) {
			(*v)--;
			if (av == 0) {
				*v -= 2;
			}
		}
		if (*v < 0) {
			*v = 0;
		}
	}
}

void Joystick_mv_anapad_psp(void)
{
	_get_anapad_sub(JoyAnaPadX, &kbd_x, 450);
	_get_anapad_sub(JoyAnaPadY, &kbd_y, 250);

}
static void mouse_update_psp(SceCtrlData psppad)
{
	int x = 128, y = 128; // origin is 128

	_get_anapad_sub(JoyAnaPadX, &x, 255);
	_get_anapad_sub(JoyAnaPadY, &y, 255);
	
	x -= 128; //x,y:(-4..4)
	y -= 128;

	if (psppad.Buttons & PSP_CTRL_LEFT) {
		x--;
	}
	if (psppad.Buttons & PSP_CTRL_RIGHT) {
		x++;
	}
	if (psppad.Buttons & PSP_CTRL_UP) {
		y--;
	}
	if (psppad.Buttons & PSP_CTRL_DOWN) {
		y++;
	}

	if (x != 0 || y != 0) {
		printf("mouse x: %d y: %d", x, y);
		Mouse_Event(0, (float)x * 0.1 * Config.MouseSpeed, (float)y * 0.1 * Config.MouseSpeed);
	}
}
#endif

