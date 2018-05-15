﻿using System.Threading;
using Core.Models;
using Core.DBConnection;
using System.Windows;
using Core.DeviceCommunication;
using System;
using ProbeSniffer.Windows;
using System.Windows.Input;
using System.IO;

namespace ProbeSniffer
{
    public class Program
    {
        private Configuration configuration = null;
        private DatabaseConnection dbConnection = null;
        private DeviceConnectionManager deviceCommunication = null;
        private System.Windows.Forms.NotifyIcon notifyIcon = null;
        #region Windows
        private SplashScreen splash = null;
        private DataVisualizer visualizer = null;
        private ToastMenu toast=null;
        #endregion

        /// <summary>
        /// Main entry point of the program
        /// </summary>
        public void Main()
        {
            bool result = false;
            
            //Showing Splash Screen
            splash = new SplashScreen();
            splash.Show();

            //Loading configuration
            splash.ShowConfLoadingSplashScreen();
            configuration = Configuration.LoadConfiguration();

            if (configuration is null)
            {
                ShowErrorMessage("Unable to load the configuration...\nExiting.");
                splash.Close();
                Application.Current.ShutdownMode = ShutdownMode.OnExplicitShutdown;
                Application.Current.Shutdown();
                return;
            }

            //Testing Database connection
            splash.ShowDBConneLoadingSplashScreen();
            dbConnection = new DatabaseConnection();
            int tries = 0;
            while (tries != 3)
            {
                dbConnection.Connect();
                if (dbConnection.Connected) break;
                Thread.Sleep(2000);
                tries++;
            }
            if (!dbConnection.Connected)    //not connected after 3 tries
            {
                ShowErrorMessage("Unable to connect to Database...\nExiting.");
                splash.Close();
                Application.Current.ShutdownMode = ShutdownMode.OnExplicitShutdown;
                Application.Current.Shutdown();
                return;
            }


            //Connecting to Devices
            splash.ShowDeviceAwaitingSplashScreen();
            deviceCommunication = new DeviceConnectionManager();
            result = deviceCommunication.Initialize(configuration.Devices);

            if (result is false)
            {
                ShowErrorMessage("Unable to initialize ESP32 devices...\nExiting.");
                splash.Close();
                Application.Current.ShutdownMode = ShutdownMode.OnExplicitShutdown;
                Application.Current.Shutdown();
                return;
            }

            //Setting up the notification icon
            notifyIcon = new System.Windows.Forms.NotifyIcon();
            notifyIcon.Text = "Probe Sniffer";
            Stream iconStream = Application.GetResourceStream(new Uri("pack://application:,,,/Assets/icon.ico")).Stream;
            notifyIcon.Icon = new System.Drawing.Icon(iconStream);
            notifyIcon.Visible = true;
            notifyIcon.Click += NotifyIcon_Click;
            toast = new ToastMenu();
            toast.MouseDoubleClick += Toast_MouseDoubleClick;
            toast.Deactivated += MenuFlyout_Deactivated;
            toast.ExitCLicked += Exit;
            toast.ShowGraphClicked += Toast_ShowGraphClicked;                   

            //Opening visualizer
            //splash.Close();
            visualizer = new DataVisualizer(configuration?.Devices);
            visualizer.Show();
        }

        private void Toast_ShowGraphClicked(object sender, RoutedEventArgs e) => visualizer.Show();

        private void ShowErrorMessage(string message) => MessageBox.Show(message,
                            "Error",
                            MessageBoxButton.OK,
                            MessageBoxImage.Error,
                            MessageBoxResult.None,
                            MessageBoxOptions.DefaultDesktopOnly);

        private void Exit(object sender, EventArgs e)
        {
            toast.Close();
            visualizer.Close();
            notifyIcon.Visible = false;
            Application.Current.Shutdown();
        }

        private void MenuFlyout_Deactivated(object sender, EventArgs e) => toast.Hide();

        private void Toast_MouseDoubleClick(object sender, MouseButtonEventArgs e) => toast.Hide();

        private void NotifyIcon_Click(object sender, EventArgs e)
        {
            toast.Left = SystemParameters.WorkArea.Width - toast.Width;
            toast.Top = SystemParameters.WorkArea.Height - toast.Height;
            toast.Opacity = 0;
            toast.Show();
            toast.Activate();
        }

    }
}
