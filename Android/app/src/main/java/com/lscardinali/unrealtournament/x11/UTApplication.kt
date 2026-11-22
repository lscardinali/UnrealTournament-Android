package com.lscardinali.unrealtournament.x11

import com.termux.x11.LorieApp
import com.termux.x11.MainActivity

/**
 * The app's Application. It has to be a LorieApp: the X server's announcement (CmdEntryPoint.ACTION_START) arrives
 * at LorieApp.Receiver, which casts the application context to LorieApp and hands the server's binder to the display
 * activity. Registered in AndroidManifest.xml.
 */
class UTApplication : LorieApp() {
    /** No "Termux:X11" notification while the game screen is up. */
    override fun onActivityResumed(activity: MainActivity) {}
}
