﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Controls;

namespace ProbeSniffer.ViewModels
{
    public class SplashViewModel: Core.ViewModelBase.BaseViewModel
    {
        #region Private

        private Page _currentPage;

        #endregion

        public Page CurrentPage
        {
            get => _currentPage;
            set
            {
                if (value == _currentPage) return;
                _currentPage = value;
                OnPropertyChanged(nameof(CurrentPage));
            }
        }

    }
}
